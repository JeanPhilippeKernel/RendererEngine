#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/SceneDiagnostics.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <uuid.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ZEngine::ECS::SceneYAML
{
    struct NodeType
    {
        enum value : uint8_t
        {
            Undefined,
            Null,
            Scalar,
            Sequence,
            Map
        };
    };
    struct EmitterStyle
    {
        enum value : uint8_t
        {
            Block,
            Flow
        };
    };

    class Storage;
    template <bool IsMap>
    class Range;

    /// A borrowed index into one document's arena-backed records. No per-node
    /// ownership/allocation. The Storage must outlive every node and range.
    class Node
    {
    public:
        Node() = default;
        NodeType::value Type() const;
        bool            IsDefined() const
        {
            return Type() != NodeType::Undefined;
        }
        bool IsNull() const
        {
            return Type() == NodeType::Null;
        }
        bool IsScalar() const
        {
            return Type() == NodeType::Scalar;
        }
        bool IsSequence() const
        {
            return Type() == NodeType::Sequence;
        }
        bool IsMap() const
        {
            return Type() == NodeType::Map;
        }
        size_t              size() const;
        std::string_view    Scalar() const;
        bool                Plain() const;
        EmitterStyle::value Style() const;
        Range<false>        Sequence() const;
        Range<true>         Mapping() const;
        Node                operator[](size_t index) const;
        Node                operator[](std::string_view key) const;
        Storage*            Owner() const
        {
            return m_owner;
        }
        void reset(const Node& node)
        {
            *this = node;
        }
        void reset()
        {
            *this = {};
        }

    private:
        friend class Storage;
        template <bool>
        friend class Range;
        Storage* m_owner = nullptr;
        uint32_t m_index = UINT32_MAX;
        Node(Storage* owner, uint32_t index) : m_owner(owner), m_index(index) {}
    };

    struct Field
    {
        std::string_view Key   = {};
        Node             Value = {};
    };

    /// Flat, trivially copyable records and links in checked engine Arrays.
    /// Reallocation never invalidates indices or copied scalar text. All values
    /// share one bounded owner, released as a unit with no recursive destruction.
    class Storage
    {
    public:
        Storage()                                         = default;
        Storage(const Storage&)                           = delete;
        Storage&                operator=(const Storage&) = delete;
        /// Routed owners start small and grow with stable checked arena blocks,
        /// never a byte_budget-sized reservation. The route must outlive this
        /// Storage; no heap/OS fallback is used.
        [[nodiscard]] bool      Initialize(size_t byte_budget = ZMega(128), uint32_t max_nodes = 1048576, uint32_t max_items = 65536, uint32_t max_scalar = 4096, Core::Memory::IAllocationRoute* allocation_route = nullptr);
        static constexpr size_t InitialRoutedCapacity(size_t byte_budget)
        {
            return byte_budget < ZKilo(32) ? byte_budget : ZKilo(32);
        }
        /// No allocation/reservation/commit: tree records/text use this already
        /// writable, caller-owned block. Its owner must outlive this Storage and
        /// all borrowed nodes. Invalid/repeated initialization preserves state.
        [[nodiscard]] bool InitializeBorrowed(void* memory, size_t bytes, uint32_t max_nodes = 1048576, uint32_t max_items = 65536, uint32_t max_scalar = 4096);
        /// Caller owns the initial block; additional stable blocks use route.
        [[nodiscard]] bool InitializeGrowingBorrowed(void* memory, size_t initial_bytes, size_t byte_budget, Core::Memory::IAllocationRoute& route, uint32_t max_nodes = 1048576, uint32_t max_items = 65536, uint32_t max_scalar = 4096);
        [[nodiscard]] bool Good() const
        {
            return m_good;
        }
        [[nodiscard]] SceneDiagnosticCode Failure() const
        {
            return m_failure;
        }
        size_t BytesUsed() const
        {
            return m_arena.m_current_offset;
        }
        size_t ByteCapacity() const
        {
            return m_arena.m_total_size;
        }
        Core::Memory::ArenaAllocator* Allocator()
        {
            return &m_arena;
        }
        // The storage's actual route wins over an unrelated nested YAML scope.
        // Native/unrouted documents may borrow the active parser budget.
        [[nodiscard]] Core::Memory::IWorkingMemoryBudget* WorkingMemoryBudget() const noexcept;
        Node                                              Destination()
        {
            return {this, UINT32_MAX};
        }
        Node               Make(NodeType::value type, EmitterStyle::value style = EmitterStyle::Block);
        Node               Scalar(std::string_view text, bool plain = false);
        Node               UInt(uint32_t value);
        Node               Float(float value);
        Node               Bool(bool value);
        Node               UUID(const uuids::uuid& value);
        /// Deep owned copy, including map keys and scalar presentation. Rejects
        /// undefined values, duplicate keys, excessive depth and budget overflow.
        [[nodiscard]] Node Clone(const Node& source, uint32_t max_depth = 32);
        [[nodiscard]] bool Set(Node map, std::string_view key, Node value);
        [[nodiscard]] bool AppendField(Node map, Node key, Node value);
        [[nodiscard]] bool Append(Node sequence, Node value);
        [[nodiscard]] bool UniqueKeys(Node map);
        void               Freeze()
        {
            m_frozen = true;
        }

        struct Checkpoint
        {
            Core::Memory::ArenaTemp Arena        = {};
            size_t                  Nodes        = 0;
            size_t                  NodeCapacity = 0;
            void*                   NodeData     = nullptr;
            size_t                  Links        = 0;
            size_t                  LinkCapacity = 0;
            void*                   LinkData     = nullptr;
            bool                    Good         = false;
            bool                    Frozen       = false;
            SceneDiagnosticCode     Failure      = SceneDiagnosticCode::None;
        };
        Checkpoint Mark();
        void       Rollback(const Checkpoint& checkpoint);

    private:
        friend class Node;
        template <bool>
        friend class Range;
        struct Record
        {
            std::string_view    Text  = {};
            uint32_t            First = UINT32_MAX;
            uint32_t            Last  = UINT32_MAX;
            uint32_t            Count = 0;
            NodeType::value     Type  = NodeType::Undefined;
            EmitterStyle::value Style = EmitterStyle::Block;
            bool                Plain = false;
        };
        struct Link
        {
            uint32_t Key   = UINT32_MAX;
            uint32_t Value = UINT32_MAX;
            uint32_t Next  = UINT32_MAX;
        };
        struct RoutedBacking
        {
            Core::Memory::IAllocationRoute* Route   = nullptr;
            void*                           Pointer = nullptr;
            ~RoutedBacking()
            {
                if (Pointer)
                {
                    Route->Free(Pointer);
                }
            }
        };
        // Declared first, freed last: arrays and borrowed arena must finish
        // before the optional routed backing block is returned to its owner.
        RoutedBacking                   m_backing    = {};
        Core::Memory::ArenaAllocator    m_arena      = {};
        Core::Containers::Array<Record> m_nodes      = {};
        Core::Containers::Array<Link>   m_links      = {};
        uint32_t                        m_max_nodes  = 0;
        uint32_t                        m_max_items  = 0;
        uint32_t                        m_max_scalar = 0;
        bool                            m_good       = false;
        bool                            m_frozen     = false;
        SceneDiagnosticCode             m_failure    = SceneDiagnosticCode::None;
        [[nodiscard]] bool              InitializeStorage(void* memory, size_t byte_budget, uint32_t max_nodes, uint32_t max_items, uint32_t max_scalar, Core::Memory::IAllocationRoute* allocation_route = nullptr, size_t initial_bytes = 0);
        bool                            Owns(Node node) const;
        bool                            AddLink(Node parent, uint32_t key, Node value);
        bool                            Contains(Node node, Node target);
        bool                            ContainsChildren(Node node, Node target);
        Node                            CloneTree(const Node& source, uint32_t max_depth);
        bool                            Fail(SceneDiagnosticCode code = SceneDiagnosticCode::InvalidData)
        {
            m_good = false;
            if (m_failure == SceneDiagnosticCode::None)
            {
                m_failure = code;
            }
            return false;
        }
        bool FailAllocation()
        {
            return Fail(m_arena.LastFailure().Kind == Core::Memory::ArenaAllocationFailureKind::CommitFailed ? SceneDiagnosticCode::AllocationFailed : SceneDiagnosticCode::LimitExceeded);
        }
    };

    template <bool IsMap>
    class Range
    {
    public:
        class Iterator
        {
        public:
            Iterator() = default;
            auto operator*() const
            {
                const auto& link = m_owner->m_links[m_link];
                if constexpr (IsMap)
                {
                    return Field{m_owner->m_nodes[link.Key].Text, Node(m_owner, link.Value)};
                }
                else
                {
                    return Node(m_owner, link.Value);
                }
            }
            Iterator& operator++()
            {
                m_link = m_owner->m_links[m_link].Next;
                return *this;
            }
            bool operator==(const Iterator& other) const
            {
                return m_owner == other.m_owner && m_link == other.m_link;
            }
            bool operator!=(const Iterator& other) const
            {
                return !(*this == other);
            }

        private:
            friend class Range;
            Storage* m_owner = nullptr;
            uint32_t m_link  = UINT32_MAX;
            Iterator(Storage* owner, uint32_t link) : m_owner(owner), m_link(link) {}
        };
        Iterator begin() const
        {
            return {m_owner, m_first};
        }
        Iterator end() const
        {
            return {m_owner, UINT32_MAX};
        }
        size_t size() const
        {
            return m_size;
        }
        bool empty() const
        {
            return m_size == 0;
        }
        auto front() const
        {
            return *begin();
        }

    private:
        friend class Node;
        Storage* m_owner = nullptr;
        uint32_t m_first = UINT32_MAX;
        uint32_t m_size  = 0;
        Range(Storage* owner, uint32_t first, uint32_t size) : m_owner(owner), m_first(first), m_size(size) {}
    };

    enum Event : uint8_t
    {
        BeginMap,
        EndMap,
        BeginSeq,
        EndSeq,
        Key,
        Value
    };

    /// Bounded, status-returning libyaml event writer.
    class Emitter
    {
    public:
        explicit Emitter(size_t max_bytes);
        ~Emitter() noexcept;
        Emitter(const Emitter&)            = delete;
        Emitter& operator=(const Emitter&) = delete;
        Emitter& operator<<(Event event);
        Emitter& operator<<(const Node& node);
        Emitter& operator<<(std::string_view value);
        Emitter& operator<<(const std::string& value)
        {
            return *this << std::string_view(value);
        }
        Emitter& operator<<(const char* value)
        {
            return *this << std::string_view(value ? value : "");
        }
        Emitter&           operator<<(uint32_t value);
        bool               good() const;
        [[nodiscard]] bool Output(std::string* out);

    private:
        struct Impl;
        ZEngine::Helpers::Scope<Impl> m_impl;
    };
} // namespace ZEngine::ECS::SceneYAML
