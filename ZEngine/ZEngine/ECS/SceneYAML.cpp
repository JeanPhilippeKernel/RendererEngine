#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Containers/HeapSort.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/ECS/SceneYAML.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <yaml.h>
#include <charconv>
#include <climits>
#include <cmath>
#include <new>

namespace ZEngine::ECS::SceneYAML
{
    bool Storage::Initialize(size_t byte_budget, uint32_t max_nodes, uint32_t max_items, uint32_t max_scalar, Core::Memory::IAllocationRoute* allocation_route)
    {
        return InitializeStorage(nullptr, byte_budget, max_nodes, max_items, max_scalar, allocation_route);
    }
    bool Storage::InitializeBorrowed(void* memory, size_t bytes, uint32_t max_nodes, uint32_t max_items, uint32_t max_scalar)
    {
        return memory && InitializeStorage(memory, bytes, max_nodes, max_items, max_scalar);
    }
    bool Storage::InitializeGrowingBorrowed(void* memory, size_t initial_bytes, size_t byte_budget, Core::Memory::IAllocationRoute& route, uint32_t max_nodes, uint32_t max_items, uint32_t max_scalar)
    {
        return memory && initial_bytes && initial_bytes <= byte_budget && InitializeStorage(memory, byte_budget, max_nodes, max_items, max_scalar, &route, initial_bytes);
    }
    bool Storage::InitializeStorage(void* memory, size_t byte_budget, uint32_t max_nodes, uint32_t max_items, uint32_t max_scalar, Core::Memory::IAllocationRoute* allocation_route, size_t initial_bytes)
    {
        if (m_arena.m_memory || m_backing.Pointer || !byte_budget || byte_budget > ZMega(256) || !max_nodes || max_nodes > 2097152 || !max_items || max_items > 65536 || !max_scalar || max_scalar > 4096)
        {
            return false;
        }
        const bool owns_backing = allocation_route && !memory;
        if (owns_backing)
        {
            initial_bytes = InitialRoutedCapacity(byte_budget);
            memory        = allocation_route->Allocate(initial_bytes);
            if (!memory)
            {
                return false;
            }
        }
        if (memory)
        {
            const bool initialized = allocation_route ? m_arena.InitializeGrowingBorrowed(memory, initial_bytes, byte_budget, *allocation_route, "SceneYAML/RoutedDocument") : m_arena.InitializeBorrowed(memory, byte_budget, "SceneYAML/BorrowedDocument");
            if (!initialized)
            {
                if (owns_backing)
                {
                    allocation_route->Free(memory);
                }
                return false;
            }
            if (owns_backing)
            {
                m_backing.Route   = allocation_route;
                m_backing.Pointer = memory;
            }
        }
        else
        {
            m_arena.Initialize(byte_budget, 0, "SceneYAML/Document");
        }
        if (!m_arena.m_memory)
        {
            return false;
        }
        m_nodes.m_allocator = &m_arena;
        m_links.m_allocator = &m_arena;
        m_max_nodes         = max_nodes;
        m_max_items         = max_items;
        m_max_scalar        = max_scalar;
        m_backing.Route     = allocation_route;
        m_good              = true;
        return true;
    }
    Core::Memory::IWorkingMemoryBudget* Storage::WorkingMemoryBudget() const noexcept
    {
        return m_backing.Route ? m_backing.Route->WorkingMemoryBudget() : Core::Serialization::YAMLWorkingMemoryBudget();
    }
    bool Storage::Owns(Node node) const
    {
        return node.m_owner == this && node.m_index < m_nodes.size();
    }
    Node Storage::Make(NodeType::value type, EmitterStyle::value style)
    {
        if (!m_good || m_frozen)
        {
            Fail();
            return {};
        }
        if (m_nodes.size() >= m_max_nodes)
        {
            Fail(SceneDiagnosticCode::LimitExceeded);
            return {};
        }
        Record     record = {.Type = type, .Style = style};
        const auto index  = static_cast<uint32_t>(m_nodes.size());
        if (!m_nodes.try_push(record))
        {
            FailAllocation();
            return {};
        }
        return {this, index};
    }
    Node Storage::Scalar(std::string_view text, bool plain)
    {
        if (!m_good || m_frozen || text.find('\0') != std::string_view::npos)
        {
            Fail();
            return {};
        }
        if (text.size() > m_max_scalar)
        {
            Fail(SceneDiagnosticCode::LimitExceeded);
            return {};
        }
        const Node node = Make(NodeType::Scalar);
        if (!node.IsDefined())
        {
            return {};
        }
        char* copy = static_cast<char*>(m_arena.TryResize(nullptr, 0, text.size() + 1, 1));
        if (!copy)
        {
            FailAllocation();
            return {};
        }
        if (!text.empty())
        {
            Helpers::secure_memcpy(copy, text.size() + 1, text.data(), text.size());
        }
        copy[text.size()]           = '\0';
        m_nodes[node.m_index].Text  = {copy, text.size()};
        m_nodes[node.m_index].Plain = plain;
        return node;
    }
    Node Storage::UInt(uint32_t value)
    {
        char       text[16] = {};
        const auto result   = std::to_chars(text, text + sizeof(text), value);
        if (result.ec != std::errc{})
        {
            Fail();
            return {};
        }
        return Scalar({text, static_cast<size_t>(result.ptr - text)}, true);
    }
    Node Storage::Float(float value)
    {
        char       text[64] = {};
        // The default general format selects the shortest representation that
        // round-trips to this float, keeping authored scene diffs readable.
        const auto result   = std::to_chars(text, text + sizeof(text), value);
        if (!std::isfinite(value) || result.ec != std::errc{})
        {
            Fail();
            return {};
        }
        return Scalar({text, static_cast<size_t>(result.ptr - text)}, true);
    }
    Node Storage::Bool(bool value)
    {
        return Scalar(value ? "true" : "false", true);
    }
    Node Storage::UUID(const uuids::uuid& value)
    {
        if (value.is_nil())
        {
            return Make(NodeType::Null);
        }
        char text[36] = {};
        uuids::to_string<char>(value, text);
        return Scalar({text, sizeof(text)});
    }
    Node Storage::Clone(const Node& source, uint32_t max_depth)
    {
        if (!m_good || m_frozen || !max_depth || max_depth > 32 || !source.IsDefined())
        {
            Fail();
            return {};
        }
        if (source.IsScalar())
        {
            return Scalar(source.Scalar(), source.Plain());
        }
        if (source.IsNull() || ((source.IsMap() || source.IsSequence()) && source.size() == 0))
        {
            return Make(source.Type(), source.Style());
        }
        return CloneTree(source, max_depth);
    }
    Node Storage::CloneTree(const Node& source, uint32_t max_depth)
    {
        struct Walk
        {
            struct Frame
            {
                Node     Source = {};
                Node     Output = {};
                Node     Key    = {};
                uint32_t Next   = UINT32_MAX;
            };
            Core::Containers::FixedArray<Frame, 32> Parents = {};
            Node                                    Current = {};
            Node                                    Copy    = {};
            Node                                    Root    = {};
            uint32_t                                Active  = 0;
            uint32_t                                Visited = 0;
        };
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(WorkingMemoryBudget(), sizeof(Walk) + sizeof(fixed)))
        {
            Fail(SceneDiagnosticCode::LimitExceeded);
            return {};
        }
        Walk walk    = {};
        walk.Current = source;
        for (;;)
        {
            if (!walk.Current.IsDefined())
            {
                Fail();
                return {};
            }
            const bool collection = walk.Current.IsMap() || walk.Current.IsSequence();
            if (++walk.Visited > m_max_nodes || (collection && walk.Active >= max_depth))
            {
                Fail(SceneDiagnosticCode::LimitExceeded);
                return {};
            }
            walk.Copy = walk.Current.IsScalar() ? Scalar(walk.Current.Scalar(), walk.Current.Plain()) : Make(walk.Current.Type(), walk.Current.Style());
            if (!walk.Copy.IsDefined())
            {
                return {};
            }
            if (walk.Active)
            {
                const auto& parent = walk.Parents[walk.Active - 1];
                // Each copy is newly owned and still empty when attached.
                // Checked append therefore needs no completed-subtree cycle
                // scan, even when cloning within this same storage owner.
                if (!(parent.Source.IsMap() ? AppendField(parent.Output, parent.Key, walk.Copy) : Append(parent.Output, walk.Copy)))
                {
                    return {};
                }
            }
            else
            {
                walk.Root = walk.Copy;
            }
            if (collection)
            {
                auto& parent  = walk.Parents[walk.Active++];
                parent.Source = walk.Current;
                parent.Output = walk.Copy;
                parent.Next   = walk.Current.m_owner->m_nodes[walk.Current.m_index].First;
            }
            while (walk.Active)
            {
                auto& parent = walk.Parents[walk.Active - 1];
                if (parent.Next != UINT32_MAX)
                {
                    auto*      owner = parent.Source.m_owner;
                    // Copy the link before Scalar/Make can grow either array
                    // during a same-owner clone. Text and indices stay stable.
                    const auto link  = owner->m_links[parent.Next];
                    parent.Next      = link.Next;
                    if (parent.Source.IsMap())
                    {
                        parent.Key = Scalar(owner->m_nodes[link.Key].Text);
                        if (!parent.Key.IsDefined())
                        {
                            return {};
                        }
                    }
                    walk.Current = Node(owner, link.Value);
                    break;
                }
                if (parent.Source.IsMap() && !UniqueKeys(parent.Output))
                {
                    return {};
                }
                --walk.Active;
            }
            if (!walk.Active)
            {
                return walk.Root;
            }
        }
    }
    bool Storage::Contains(Node node, Node target)
    {
        if (node.m_index == target.m_index)
        {
            return true;
        }
        // Most inserted children are scalars or newly opened empty containers.
        // Neither can reach another node, so no traversal state is needed.
        if (m_nodes[node.m_index].First == UINT32_MAX)
        {
            return false;
        }
        return ContainsChildren(node, target);
    }
    bool Storage::ContainsChildren(Node node, Node target)
    {
        struct Walk
        {
            // Original cycle checks allow depth zero through 32 inclusive.
            Core::Containers::FixedArray<uint32_t, 33> Next    = {};
            uint32_t                                   Current = 0;
            uint32_t                                   Active  = 0;
            uint32_t                                   Visited = 0;
        };
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(WorkingMemoryBudget(), sizeof(Walk) + sizeof(fixed)))
        {
            Fail(SceneDiagnosticCode::LimitExceeded);
            return true; // Fail closed; the caller must not install this link.
        }
        Walk walk    = {};
        walk.Current = node.m_index;
        for (;;)
        {
            if (walk.Current == target.m_index || walk.Active > 32 || ++walk.Visited > m_max_nodes)
            {
                return true;
            }
            const auto first = m_nodes[walk.Current].First;
            if (first != UINT32_MAX)
            {
                walk.Next[walk.Active++] = first;
            }
            while (walk.Active && walk.Next[walk.Active - 1] == UINT32_MAX)
            {
                --walk.Active;
            }
            if (!walk.Active)
            {
                return false;
            }
            auto& next   = walk.Next[walk.Active - 1];
            walk.Current = m_links[next].Value;
            next         = m_links[next].Next;
        }
    }
    bool Storage::AddLink(Node parent, uint32_t key, Node value)
    {
        if (!m_good || m_frozen || !Owns(parent) || !Owns(value))
        {
            return Fail();
        }
        if (m_nodes[parent.m_index].Count >= m_max_items)
        {
            return Fail(SceneDiagnosticCode::LimitExceeded);
        }
        if (Contains(value, parent))
        {
            return Fail();
        }
        const auto index = static_cast<uint32_t>(m_links.size());
        if (!m_links.try_push({key, value.m_index}))
        {
            return FailAllocation();
        }
        auto& record = m_nodes[parent.m_index];
        if (record.Last == UINT32_MAX)
        {
            record.First = index;
        }
        else
        {
            m_links[record.Last].Next = index;
        }
        record.Last = index;
        ++record.Count;
        return true;
    }
    bool Storage::Append(Node sequence, Node value)
    {
        if (!sequence.IsSequence())
        {
            return Fail();
        }
        return AddLink(sequence, UINT32_MAX, value);
    }
    bool Storage::AppendField(Node map, Node key, Node value)
    {
        if (!map.IsMap() || !Owns(key) || !key.IsScalar())
        {
            return Fail();
        }
        return AddLink(map, key.m_index, value);
    }
    bool Storage::Set(Node map, std::string_view key, Node value)
    {
        if (!m_good || m_frozen || !Owns(map) || !map.IsMap() || !Owns(value))
        {
            return Fail();
        }
        for (auto link = m_nodes[map.m_index].First; link != UINT32_MAX; link = m_links[link].Next)
        {
            if (m_nodes[m_links[link].Key].Text == key)
            {
                if (Contains(value, map))
                {
                    return Fail();
                }
                m_links[link].Value = value.m_index;
                return true;
            }
        }
        const auto name = Scalar(key);
        return name.IsDefined() && AppendField(map, name, value);
    }
    bool Storage::UniqueKeys(Node map)
    {
        if (!m_good || !Owns(map) || !map.IsMap())
        {
            return Fail();
        }
        // Empty component maps, single-field payloads and two-field entity /
        // component envelopes dominate scene input. Their uniqueness needs no
        // temporary array, arena reservation or sorting helper state.
        if (map.size() < 2)
        {
            return true;
        }
        if (map.size() == 2)
        {
            const auto first  = m_nodes[map.m_index].First;
            const auto second = m_links[first].Next;
            return m_nodes[m_links[first].Key].Text != m_nodes[m_links[second].Key].Text || Fail();
        }
        Core::Memory::WorkingMemoryCharge fixed = {};
        if (!fixed.TryAcquire(WorkingMemoryBudget(), sizeof(Core::Containers::HeapSortState<uint32_t>) + sizeof(Core::Containers::Array<uint32_t>) + sizeof(Core::Memory::ArenaTemp) + sizeof(fixed)))
        {
            return Fail(SceneDiagnosticCode::LimitExceeded);
        }
        Core::Containers::HeapSortState<uint32_t> sort = {};
        const auto                                mark = Core::Memory::BeginTempArena(&m_arena);
        Core::Containers::Array<uint32_t>         keys = {};
        keys.m_allocator                               = &m_arena;
        if (!keys.try_reserve(map.size()))
        {
            Core::Memory::EndTempArena(mark);
            return FailAllocation();
        }
        for (auto link = m_nodes[map.m_index].First; link != UINT32_MAX; link = m_links[link].Next)
        {
            keys.push(m_links[link].Key);
        }
        Core::Containers::HeapSort<uint32_t>(keys, sort, [&](uint32_t left, uint32_t right) { return m_nodes[left].Text < m_nodes[right].Text; });
        bool unique = true;
        for (size_t i = 1; i < keys.size(); ++i)
        {
            if (m_nodes[keys[i - 1]].Text == m_nodes[keys[i]].Text)
            {
                unique = false;
                break;
            }
        }
        Core::Memory::EndTempArena(mark);
        return unique || Fail();
    }
    Storage::Checkpoint Storage::Mark()
    {
        return {Core::Memory::BeginTempArena(&m_arena), m_nodes.size(), m_nodes.capacity(), m_nodes.data(), m_links.size(), m_links.capacity(), m_links.data(), m_good, m_frozen, m_failure};
    }
    void Storage::Rollback(const Checkpoint& mark)
    {
        Core::Memory::EndTempArena(mark.Arena);
        m_nodes.m_data     = static_cast<Record*>(mark.NodeData);
        m_nodes.m_size     = mark.Nodes;
        m_nodes.m_capacity = mark.NodeCapacity;
        m_links.m_data     = static_cast<Link*>(mark.LinkData);
        m_links.m_size     = mark.Links;
        m_links.m_capacity = mark.LinkCapacity;
        m_good             = mark.Good;
        m_frozen           = mark.Frozen;
        m_failure          = mark.Failure;
    }
    NodeType::value Node::Type() const
    {
        return m_owner && m_owner->Owns(*this) ? m_owner->m_nodes[m_index].Type : NodeType::Undefined;
    }
    size_t Node::size() const
    {
        return IsDefined() ? m_owner->m_nodes[m_index].Count : 0;
    }
    std::string_view Node::Scalar() const
    {
        return IsScalar() ? m_owner->m_nodes[m_index].Text : std::string_view{};
    }
    bool Node::Plain() const
    {
        return IsDefined() && m_owner->m_nodes[m_index].Plain;
    }
    EmitterStyle::value Node::Style() const
    {
        return IsDefined() ? m_owner->m_nodes[m_index].Style : EmitterStyle::Block;
    }
    Range<false> Node::Sequence() const
    {
        return IsSequence() ? Range<false>{m_owner, m_owner->m_nodes[m_index].First, static_cast<uint32_t>(size())} : Range<false>{m_owner, UINT32_MAX, 0};
    }
    Range<true> Node::Mapping() const
    {
        return IsMap() ? Range<true>{m_owner, m_owner->m_nodes[m_index].First, static_cast<uint32_t>(size())} : Range<true>{m_owner, UINT32_MAX, 0};
    }
    Node Node::operator[](size_t index) const
    {
        if (!IsSequence() || index >= size())
        {
            return {};
        }
        auto link = m_owner->m_nodes[m_index].First;
        while (index--)
        {
            link = m_owner->m_links[link].Next;
        }
        return {m_owner, m_owner->m_links[link].Value};
    }
    Node Node::operator[](std::string_view key) const
    {
        for (const auto field : Mapping())
        {
            if (field.Key == key)
            {
                return field.Value;
            }
        }
        return {};
    }

    struct Emitter::Impl
    {
        yaml_emitter_t Writer      = {};
        std::string    Text        = {};
        size_t         Limit       = 0;
        bool           Initialized = false;
        bool           Valid       = false;
        bool           Finished    = false;

        explicit Impl(size_t limit) : Limit(limit)
        {
            Initialized = yaml_emitter_initialize(&Writer) != 0;
            Valid       = Initialized;
            if (!Valid)
            {
                return;
            }
            yaml_emitter_set_output(
                &Writer,
                [](void* context, unsigned char* bytes, size_t size) {
                    auto& output = *static_cast<Impl*>(context);
                    if (size > output.Limit - output.Text.size())
                    {
                        return 0;
                    }
                    output.Text.append(reinterpret_cast<const char*>(bytes), size);
                    return 1;
                },
                this);
            yaml_emitter_set_unicode(&Writer, 1);
            yaml_emitter_set_width(&Writer, -1);
            yaml_event_t event = {};
            if (!yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING))
            {
                Valid = false;
                return;
            }
            if (!Send(event))
            {
                return;
            }
            if (!yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 1))
            {
                Valid = false;
            }
            else
            {
                Send(event);
            }
        }
        ~Impl()
        {
            if (Initialized)
            {
                yaml_emitter_delete(&Writer);
            }
        }

        bool Send(yaml_event_t& event)
        {
            if (!Valid || Finished)
            {
                yaml_event_delete(&event);
                return false;
            }
            Valid = yaml_emitter_emit(&Writer, &event) != 0;
            return Valid;
        }
        bool Collection(Event type, bool flow = false)
        {
            if (!Valid || Finished)
            {
                return false;
            }
            yaml_event_t event       = {};
            int          initialized = 0;
            switch (type)
            {
                case BeginMap:
                    initialized = yaml_mapping_start_event_initialize(&event, nullptr, nullptr, 1, flow ? YAML_FLOW_MAPPING_STYLE : YAML_BLOCK_MAPPING_STYLE);
                    break;
                case EndMap:
                    initialized = yaml_mapping_end_event_initialize(&event);
                    break;
                case BeginSeq:
                    initialized = yaml_sequence_start_event_initialize(&event, nullptr, nullptr, 1, flow ? YAML_FLOW_SEQUENCE_STYLE : YAML_BLOCK_SEQUENCE_STYLE);
                    break;
                case EndSeq:
                    initialized = yaml_sequence_end_event_initialize(&event);
                    break;
                default:
                    return true;
            }
            if (!initialized)
            {
                Valid = false;
                return false;
            }
            return Send(event);
        }
        bool Scalar(std::string_view text, bool plain)
        {
            if (!Valid || Finished)
            {
                return false;
            }
            if (text.size() > INT_MAX)
            {
                Valid = false;
                return false;
            }
            yaml_event_t event = {};
            const auto*  bytes = reinterpret_cast<const yaml_char_t*>(text.empty() ? "" : text.data());
            if (!yaml_scalar_event_initialize(&event, nullptr, nullptr, bytes, static_cast<int>(text.size()), 1, 1, plain ? YAML_PLAIN_SCALAR_STYLE : YAML_DOUBLE_QUOTED_SCALAR_STYLE))
            {
                Valid = false;
                return false;
            }
            return Send(event);
        }
        bool Emit(const Node& node, uint32_t depth = 0)
        {
            if (!Valid || depth > 32)
            {
                Valid = false;
                return false;
            }
            if (node.IsNull())
            {
                return Scalar("null", true);
            }
            if (node.IsScalar())
            {
                return Scalar(node.Scalar(), node.Plain());
            }
            if (node.IsSequence())
            {
                if (!Collection(BeginSeq, node.Style() == EmitterStyle::Flow))
                {
                    return false;
                }
                for (const auto& item : node.Sequence())
                {
                    if (!Emit(item, depth + 1))
                    {
                        return false;
                    }
                }
                return Collection(EndSeq);
            }
            if (node.IsMap())
            {
                if (!Collection(BeginMap, node.Style() == EmitterStyle::Flow))
                {
                    return false;
                }
                for (const auto& entry : node.Mapping())
                {
                    if (!Scalar(entry.Key, false) || !Emit(entry.Value, depth + 1))
                    {
                        return false;
                    }
                }
                return Collection(EndMap);
            }
            Valid = false;
            return false;
        }
    };

    Emitter::Emitter(size_t limit) : m_impl(new (std::nothrow) Impl(limit)) {}
    Emitter::~Emitter() noexcept = default;
    Emitter& Emitter::operator<<(Event event)
    {
        if (m_impl)
        {
            m_impl->Collection(event);
        }
        return *this;
    }
    Emitter& Emitter::operator<<(const Node& node)
    {
        if (m_impl)
        {
            m_impl->Emit(node);
        }
        return *this;
    }
    Emitter& Emitter::operator<<(std::string_view text)
    {
        if (m_impl)
        {
            m_impl->Scalar(text, false);
        }
        return *this;
    }
    Emitter& Emitter::operator<<(uint32_t value)
    {
        char       text[16] = {};
        const auto result   = std::to_chars(text, text + sizeof(text), value);
        if (m_impl)
        {
            m_impl->Scalar({text, static_cast<size_t>(result.ptr - text)}, true);
        }
        return *this;
    }
    bool Emitter::good() const
    {
        return m_impl && m_impl->Valid;
    }
    bool Emitter::Output(std::string* out)
    {
        if (!m_impl)
        {
            return false;
        }
        auto& impl = *m_impl;
        if (!out || !impl.Valid || impl.Finished)
        {
            return false;
        }
        yaml_event_t event = {};
        if (!yaml_document_end_event_initialize(&event, 1))
        {
            impl.Valid = false;
        }
        else
        {
            impl.Send(event);
        }
        if (!impl.Valid)
        {
            return false;
        }
        if (!yaml_stream_end_event_initialize(&event))
        {
            impl.Valid = false;
        }
        else
        {
            impl.Send(event);
        }
        if (!impl.Valid)
        {
            return false;
        }
        impl.Finished = true;
        *out          = std::move(impl.Text);
        return true;
    }
} // namespace ZEngine::ECS::SceneYAML
