#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookInputPath.h>
#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/Cook/CookStorageLayout.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/UTF8.h>
#include <rapidhash.h>
#include <algorithm>
#include <new>
#include <type_traits>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        uint32_t Grow(uint32_t current, uint32_t required, uint32_t limit, uint32_t initial)
        {
            if (required <= current)
                return current;
            current = current ? current : std::min(initial, limit);
            while (current < required)
                current = current > limit / 2 ? limit : current * 2;
            return current;
        }

        uint32_t HashCapacity(uint32_t nodes)
        {
            uint32_t count = nodes ? 2 : 0;
            while (count < nodes * 2)
                count *= 2;
            return count;
        }

        uint32_t IdentityBucket(const CookAssetIdentity& identity, uint32_t capacity)
        {
            const auto bytes = identity.UUID.as_bytes();
            return static_cast<uint32_t>(rapidhash_withSeed(bytes.data(), bytes.size(), static_cast<uint64_t>(identity.Kind))) & (capacity - 1);
        }

        bool ValidProvenance(Core::Containers::StringView schema, Core::Containers::StringView field)
        {
            return schema.size() && schema.size() < sizeof(CookGraphOccurrence{}.SchemaKey) && field.size() && field.size() < sizeof(CookGraphOccurrence{}.FieldKey) && Helpers::IsValidUTF8(schema.data(), schema.size(), false) && Helpers::IsValidUTF8(field.data(), field.size(), false);
        }

        template <typename T>
        void CopyRecords(T* destination, const T* source, uint32_t count)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            for (uint32_t i = 0; i < count; ++i)
                new (destination + i) T(source[i]);
        }
    } // namespace

    CookDependencyGraph::~CookDependencyGraph()
    {
        Reset();
    }

    VFS::VFSResult<void> CookDependencyGraph::Initialize(ICookAllocator& allocator, const CookLimits& limits)
    {
        if (m_state != State::Empty || ValidateCookLimits(limits).Failed())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        m_allocator = &allocator;
        m_limits    = limits;
        m_state     = State::Roots;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookDependencyGraph::Ensure(uint32_t nodes, uint32_t edges, uint32_t scenes, uint32_t occurrences, uint32_t text)
    {
        constexpr uint32_t text_per_edge = 128 + VFS::VFS_MAX_PATH;
        const uint32_t     text_limit    = m_limits.MaxEdges * text_per_edge;
        if (nodes > m_limits.MaxAssets || edges > m_limits.MaxEdges || scenes > m_limits.MaxScenes || occurrences > m_limits.MaxReferenceOccurrences || text > text_limit)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        Storage pending            = {};
        pending.NodeCapacity       = Grow(m_storage.NodeCapacity, nodes, m_limits.MaxAssets, 4);
        pending.EdgeCapacity       = Grow(m_storage.EdgeCapacity, edges, m_limits.MaxEdges, 8);
        pending.SceneCapacity      = Grow(m_storage.SceneCapacity, scenes, m_limits.MaxScenes, 1);
        pending.OccurrenceCapacity = Grow(m_storage.OccurrenceCapacity, occurrences, m_limits.MaxReferenceOccurrences, 4);
        pending.TextCapacity       = Grow(m_storage.TextCapacity, text, text_limit, 256);
        pending.HashCapacity       = HashCapacity(pending.NodeCapacity);
        if (pending.NodeCapacity == m_storage.NodeCapacity && pending.EdgeCapacity == m_storage.EdgeCapacity && pending.SceneCapacity == m_storage.SceneCapacity && pending.OccurrenceCapacity == m_storage.OccurrenceCapacity && pending.TextCapacity == m_storage.TextCapacity)
            return VFS::VFSResult<void>::Ok();
        // Limits are validated against the small fixed v1 hard caps. Still use
        // checked layout arithmetic; no raw struct or pointer becomes a key.
        Detail::CookStorageLayout storage_layout = {};
        bool                      overflow       = false;
        const auto                segment        = [&]<typename T>(T*& pointer, uint32_t count, uint8_t* block) {
            size_t offset = 0;
            if (overflow || !storage_layout.Add<T>(count, &offset))
            {
                overflow = true;
                return;
            }
            pointer = block && count ? reinterpret_cast<T*>(block + offset) : nullptr;
        };
        const auto layout = [&](uint8_t* block) {
            segment(pending.Nodes, pending.NodeCapacity, block);
            segment(pending.Edges, pending.EdgeCapacity, block);
            segment(pending.Scenes, pending.SceneCapacity, block);
            segment(pending.Occurrences, pending.OccurrenceCapacity, block);
            segment(pending.Hash, pending.HashCapacity, block);
            segment(pending.Canonical, pending.NodeCapacity, block);
            segment(pending.LeafFirst, pending.NodeCapacity, block);
            segment(pending.Pending, pending.NodeCapacity, block);
            segment(pending.Heads, pending.NodeCapacity, block);
            segment(pending.Heap, pending.NodeCapacity, block);
            segment(pending.Parents, pending.EdgeCapacity, block);
            segment(pending.Next, pending.EdgeCapacity, block);
            segment(pending.ByOccurrence, pending.OccurrenceCapacity, block);
            segment(pending.Roots, pending.OccurrenceCapacity, block);
            segment(pending.Text, pending.TextCapacity, block);
        };
        layout(nullptr);
        if (overflow)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        const auto allocated = m_allocator->Allocate(storage_layout.Size(), DEFAULT_ALIGNMENT, CookMemoryCategory::CatalogGraph);
        if (allocated.Failed())
            return VFS::VFSResult<void>::Fail(allocated.Error());
        if (!allocated.Value())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::OutOfMemory);
        if (reinterpret_cast<uintptr_t>(allocated.Value()) % DEFAULT_ALIGNMENT)
        {
            m_allocator->Free(allocated.Value());
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        pending.Block  = allocated.Value();
        storage_layout = {};
        layout(static_cast<uint8_t*>(pending.Block));
        CopyRecords(pending.Nodes, m_storage.Nodes, m_node_count);
        CopyRecords(pending.Edges, m_storage.Edges, m_edge_count);
        CopyRecords(pending.Scenes, m_storage.Scenes, m_scene_count);
        CopyRecords(pending.Occurrences, m_storage.Occurrences, m_occurrence_count);
        if (m_text_bytes)
            Helpers::secure_memcpy(pending.Text, pending.TextCapacity, m_storage.Text, m_text_bytes);
        void* previous = m_storage.Block;
        m_storage      = pending;
        Reindex();
        if (previous)
            m_allocator->Free(previous);
        return VFS::VFSResult<void>::Ok();
    }

    void CookDependencyGraph::Index(uint32_t node) noexcept
    {
        uint32_t slot = IdentityBucket(m_storage.Nodes[node].Identity, m_storage.HashCapacity);
        while (m_storage.Hash[slot] != CookGraphInvalidIndex)
            slot = (slot + 1) & (m_storage.HashCapacity - 1);
        m_storage.Hash[slot] = node;
    }

    void CookDependencyGraph::Reindex() noexcept
    {
        if (m_storage.HashCapacity)
            std::fill_n(m_storage.Hash, m_storage.HashCapacity, CookGraphInvalidIndex);
        for (uint32_t i = 0; i < m_node_count; ++i)
            Index(i);
    }

    uint32_t CookDependencyGraph::Find(const CookAssetIdentity& identity) const noexcept
    {
        if (!identity.IsValid() || !m_storage.HashCapacity)
            return CookGraphInvalidIndex;
        uint32_t slot = IdentityBucket(identity, m_storage.HashCapacity);
        while (m_storage.Hash[slot] != CookGraphInvalidIndex)
        {
            const uint32_t index = m_storage.Hash[slot];
            if (m_storage.Nodes[index].Identity == identity)
                return index;
            slot = (slot + 1) & (m_storage.HashCapacity - 1);
        }
        return CookGraphInvalidIndex;
    }

    VFS::VFSResult<uint32_t> CookDependencyGraph::Intern(const CookAssetIdentity& identity, uint32_t origin, uint32_t parent)
    {
        const auto found = Find(identity);
        if (found != CookGraphInvalidIndex)
            return VFS::VFSResult<uint32_t>::Ok(found);
        const auto reserved = Ensure(m_node_count + 1, m_edge_count, m_scene_count, m_occurrence_count, m_text_bytes);
        if (reserved.Failed())
            return VFS::VFSResult<uint32_t>::Fail(reserved.Error());
        const uint32_t index = m_node_count++;
        new (m_storage.Nodes + index) CookGraphNode{.Identity = identity, .OriginOccurrence = origin, .FirstParent = parent};
        Index(index);
        return VFS::VFSResult<uint32_t>::Ok(index);
    }

    VFS::VFSResult<uint32_t> CookDependencyGraph::AddScene(const uuids::uuid& uuid, const VFS::VFSRelativePath& source)
    {
        if (m_state != State::Roots || uuid.is_nil() || !IsCookLogicalPath(source))
            return VFS::VFSResult<uint32_t>::Fail(VFS::VFSError::InvalidPath);
        for (uint32_t i = 0; i < m_scene_count; ++i)
            if (m_storage.Scenes[i].UUID == uuid || m_storage.Scenes[i].Source == source)
                return VFS::VFSResult<uint32_t>::Fail(VFS::VFSError::AlreadyExists);
        const CookGraphScene candidate = {.UUID = uuid, .Source = source};
        const auto           reserved  = Ensure(m_node_count, m_edge_count, m_scene_count + 1, m_occurrence_count, m_text_bytes);
        if (reserved.Failed())
            return VFS::VFSResult<uint32_t>::Fail(reserved.Error());
        new (m_storage.Scenes + m_scene_count) CookGraphScene(candidate);
        return VFS::VFSResult<uint32_t>::Ok(m_scene_count++);
    }

    VFS::VFSResult<void> CookDependencyGraph::AddRoot(uint32_t scene, const CookAssetIdentity& identity, const uuids::uuid& entity, Core::Containers::StringView schema, Core::Containers::StringView field)
    {
        if (m_state != State::Roots || scene >= m_scene_count || !identity.IsValid() || !ValidProvenance(schema, field))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (m_scene_reference_count == m_limits.MaxReferenceOccurrences)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        CookGraphOccurrence     occurrence    = {.Scene = scene, .EntityUUID = entity};
        const CookAssetIdentity identity_copy = identity;
        Helpers::secure_memcpy(occurrence.SchemaKey, sizeof(occurrence.SchemaKey), schema.data(), schema.size());
        Helpers::secure_memcpy(occurrence.FieldKey, sizeof(occurrence.FieldKey), field.data(), field.size());
        const auto reserved = Ensure(m_node_count, m_edge_count, m_scene_count, m_occurrence_count + 1, m_text_bytes);
        if (reserved.Failed())
            return reserved;
        const auto node = Intern(identity_copy, m_occurrence_count, CookGraphInvalidIndex);
        if (node.Failed())
            return VFS::VFSResult<void>::Fail(node.Error());
        occurrence.Node = node.Value();
        new (m_storage.Occurrences + m_occurrence_count++) CookGraphOccurrence(occurrence);
        ++m_scene_reference_count;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<uint32_t> CookDependencyGraph::AddScene(const ECS::SceneDocument& document, const ECS::SceneComponentSchemaRegistry& schemas, const VFS::VFSRelativePath& source, CookDiagnostics* diagnostics)
    {
        if (!document.Settings() || document.ReadOnly() || !schemas.IsInitialized())
            return VFS::VFSResult<uint32_t>::Fail(VFS::VFSError::InvalidPath);
        const uint32_t previous_nodes = m_node_count, previous_occurrences = m_occurrence_count, previous_references = m_scene_reference_count;
        const auto     added = AddScene(document.Settings()->UUID, source);
        if (added.Failed())
            return added;
        struct Enumeration
        {
            CookDependencyGraph* Graph       = nullptr;
            CookDiagnostics*     Diagnostics = nullptr;
            uint32_t             Scene       = CookGraphInvalidIndex;
            VFS::VFSError        Error       = VFS::VFSError::OK;
        } state          = {this, diagnostics, added.Value()};
        const auto visit = [](void* context, const ECS::SceneReferenceOccurrence& occurrence) {
            auto&       state = *static_cast<Enumeration*>(context);
            auto&       graph = *state.Graph;
            const auto& ref   = occurrence.Reference;
            if (graph.m_scene_reference_count == graph.m_limits.MaxReferenceOccurrences)
                state.Error = VFS::VFSError::SizeLimitExceeded;
            else if (ref.Kind == ECS::SceneReferenceKind::Entity)
            {
                ++graph.m_scene_reference_count;
                return true;
            }
            else if (ref.Kind != ECS::SceneReferenceKind::Asset || ParseCookAssetKind(ref.AssetKindKey) == CookAssetKind::Unknown)
                state.Error = VFS::VFSError::Corrupted;
            else
            {
                const Core::Containers::StringView schema(occurrence.SchemaKey), field(ref.FieldKey);
                if (!ValidProvenance(schema, field))
                    state.Error = VFS::VFSError::Corrupted;
                else if (ref.UUID.is_nil())
                {
                    ++graph.m_scene_reference_count;
                    return true;
                }
                else
                {
                    const auto root = graph.AddRoot(state.Scene, {ParseCookAssetKind(ref.AssetKindKey), ref.UUID}, occurrence.EntityUUID, schema, field);
                    if (root.Succeeded())
                        return true;
                    state.Error = root.Error();
                }
            }
            const auto code = state.Error == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : state.Error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::InvalidScene;
            if (auto* entry = state.Diagnostics ? state.Diagnostics->Emplace(CookStage::Discover, code, state.Error) : nullptr)
            {
                const auto& scene = *graph.Scene(state.Scene);
                entry->Source     = scene.Source;
                entry->SceneUUID  = scene.UUID;
                entry->EntityUUID = occurrence.EntityUUID;
                entry->Asset      = {ParseCookAssetKind(ref.AssetKindKey), ref.UUID};
                (void) entry->SetKeys(occurrence.SchemaKey, ref.FieldKey);
                (void) entry->SetMessage("Scene references require bounded occurrences and stable typed asset descriptors.");
            }
            return false;
        };
        // Avoid a second large diagnostic array on the stack. This boundary
        // records fixed cook provenance; document validation owns parser detail.
        if (document.EnumerateReferences(schemas, visit, &state, nullptr))
            return added;
        if (state.Error == VFS::VFSError::OK)
        {
            state.Error = VFS::VFSError::Corrupted;
            if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Discover, CookDiagnosticCode::InvalidScene, state.Error) : nullptr)
            {
                const auto& scene = *Scene(added.Value());
                entry->Source     = scene.Source;
                entry->SceneUUID  = scene.UUID;
                (void) entry->SetMessage("Scene document reference enumeration failed or its schemas changed.");
            }
        }
        --m_scene_count;
        m_node_count            = previous_nodes;
        m_occurrence_count      = previous_occurrences;
        m_scene_reference_count = previous_references;
        Reindex();
        return VFS::VFSResult<uint32_t>::Fail(state.Error);
    }

    VFS::VFSResult<void> CookDependencyGraph::BeginDiscovery()
    {
        if (m_state != State::Roots || !m_scene_count)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        m_state = State::Discovering;
        return VFS::VFSResult<void>::Ok();
    }

    uint32_t CookDependencyGraph::StoreText(Core::Containers::StringView text)
    {
        const uint32_t offset = m_text_bytes;
        if (text.size())
        {
            Helpers::secure_memcpy(m_storage.Text + offset, m_storage.TextCapacity - offset, text.data(), text.size());
            m_text_bytes                   += static_cast<uint32_t>(text.size());
            m_storage.Text[m_text_bytes++]  = '\0';
        }
        return offset;
    }

    VFS::VFSResult<void> CookDependencyGraph::SetInspection(uint32_t node, uint32_t revision, const CookAssetInspection& inspection)
    {
        return SetInspection(node, revision, CookAssetInspectionView{inspection.Format(), inspection.LocalContentKey(), inspection.MetadataKey(), inspection.Dependencies()});
    }

    VFS::VFSResult<void> CookDependencyGraph::SetInspection(uint32_t node, uint32_t revision, const CookAssetInspectionView& inspection)
    {
        if (m_state != State::Discovering || node >= m_node_count || m_storage.Nodes[node].Inspected)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto* format = FindCookAssetFormat(inspection.Format);
        if (!format || format->Kind != m_storage.Nodes[node].Identity.Kind)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        if (format->Revision != revision)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        const auto dependencies = inspection.Dependencies;
        if (dependencies.size() && !dependencies.data())
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (dependencies.size() > m_limits.MaxEdges - m_edge_count || dependencies.size() > m_limits.MaxReferenceOccurrences - m_scene_reference_count - m_edge_count)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        for (size_t i = 0; i < dependencies.size(); ++i)
        {
            const auto& dependency = dependencies[i];
            if (!dependency.Identity.IsValid() || !dependency.Slot.size() || dependency.Slot.size() >= 128 || !Helpers::IsValidUTF8(dependency.Slot.data(), dependency.Slot.size(), false) || (dependency.SourcePath.size() && ParseCookInputPath(dependency.SourcePath).Failed()))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            if (i && !CookDependencySlotBefore(dependencies[i - 1].Slot, dependency.Slot) && !(dependencies[i - 1].Slot == dependency.Slot && dependencies[i - 1].Identity < dependency.Identity))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        uint64_t required_text = m_text_bytes;
        for (const auto& dependency : dependencies)
            required_text += dependency.Slot.size() + 1 + (dependency.SourcePath.size() ? dependency.SourcePath.size() + 1 : 0);
        if (required_text > UINT32_MAX)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        const auto reserved = Ensure(m_node_count, m_edge_count + static_cast<uint32_t>(dependencies.size()), m_scene_count, m_occurrence_count, static_cast<uint32_t>(required_text));
        if (reserved.Failed())
            return reserved;
        const uint32_t previous_nodes = m_node_count;
        // Intern first, without writing edges/text. A later refusal restores the
        // identity set/index; retained capacity remains charged and reusable.
        for (const auto& dependency : dependencies)
        {
            const auto child = Intern(dependency.Identity, m_storage.Nodes[node].OriginOccurrence, node);
            if (child.Failed())
            {
                m_node_count = previous_nodes;
                Reindex();
                return VFS::VFSResult<void>::Fail(child.Error());
            }
        }
        const uint32_t first = m_edge_count;
        for (const auto& dependency : dependencies)
        {
            new (m_storage.Edges + m_edge_count++) CookGraphEdge{Find(dependency.Identity), StoreText(dependency.Slot), static_cast<uint32_t>(dependency.Slot.size()), StoreText(dependency.SourcePath), static_cast<uint32_t>(dependency.SourcePath.size())};
        }
        auto& stored           = m_storage.Nodes[node];
        stored.LocalContentKey = inspection.LocalContentKey;
        stored.MetadataKey     = inspection.MetadataKey;
        stored.Format          = format->Key;
        stored.AdapterRevision = revision;
        stored.FirstEdge       = first;
        stored.EdgeCount       = static_cast<uint32_t>(dependencies.size());
        stored.Inspected       = true;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookDependencyGraph::Finalize(CookDiagnostics* diagnostics)
    {
        if (m_state != State::Discovering)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        for (uint32_t i = 0; i < m_node_count; ++i)
            if (!m_storage.Nodes[i].Inspected)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (!m_node_count)
        {
            m_state = State::Finalized;
            return VFS::VFSResult<void>::Ok();
        }
        // One shared principal sort/queue state, reserved before mutating any
        // finalization workspace. Refusal leaves discovery retryable.
        CookMemoryCharge fixed   = {};
        const auto       charged = m_allocator->TryCharge(sizeof(Detail::CookSortState<uint32_t>) + sizeof(fixed), CookMemoryCategory::CatalogGraph, &fixed);
        if (charged.Failed())
            return charged;
        Detail::CookSortState<uint32_t> sort = {};
        for (uint32_t i = 0; i < m_node_count; ++i)
        {
            m_storage.Canonical[i] = i;
            m_storage.Pending[i]   = m_storage.Nodes[i].EdgeCount;
            m_storage.Heads[i]     = CookGraphInvalidIndex;
            m_storage.Heap[i]      = i;
        }
        // Lookup is typed, but asset UUIDs occupy one global namespace. Reuse
        // finalization's heap storage; no secondary hash table/allocation.
        Core::Containers::HeapSort<uint32_t>({m_storage.Heap, m_node_count}, sort, [&](uint32_t left, uint32_t right) { return m_storage.Nodes[left].Identity.UUID < m_storage.Nodes[right].Identity.UUID; });
        for (uint32_t i = 1; i < m_node_count; ++i)
            if (m_storage.Nodes[m_storage.Heap[i - 1]].Identity.UUID == m_storage.Nodes[m_storage.Heap[i]].Identity.UUID)
            {
                m_state = State::Failed;
                if (auto* entry = diagnostics ? diagnostics->Emplace(CookStage::Discover, CookDiagnosticCode::IdentityConflict, VFS::VFSError::Corrupted) : nullptr)
                {
                    (void) Describe(m_storage.Heap[i], CookDiagnosticCode::IdentityConflict, "Asset UUID is reused across different asset kinds.", entry);
                    entry->Underlying = VFS::VFSError::Corrupted;
                }
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
        const auto before = [&](uint32_t a, uint32_t b) { return m_storage.Nodes[a].Identity < m_storage.Nodes[b].Identity; };
        Core::Containers::HeapSort<uint32_t>({m_storage.Canonical, m_node_count}, sort, before);
        for (uint32_t parent = 0; parent < m_node_count; ++parent)
            for (uint32_t i = 0; i < m_storage.Nodes[parent].EdgeCount; ++i)
            {
                const uint32_t edge     = m_storage.Nodes[parent].FirstEdge + i;
                const uint32_t child    = m_storage.Edges[edge].Child;
                m_storage.Parents[edge] = parent;
                m_storage.Next[edge]    = m_storage.Heads[child];
                m_storage.Heads[child]  = edge;
            }
        const auto heap_before = [&](uint32_t a, uint32_t b) { return before(b, a); };
        uint32_t   ready = 0, produced = 0;
        for (uint32_t i = 0; i < m_node_count; ++i)
            if (!m_storage.Pending[i])
                m_storage.Heap[ready++] = i;
        // Batch the initial frontier in linear time; newly unblocked parents
        // still take one logarithmic push each as their children drain.
        Core::Containers::HeapMake<uint32_t>({m_storage.Heap, ready}, sort, heap_before);
        while (ready)
        {
            Core::Containers::HeapPop<uint32_t>({m_storage.Heap, ready}, sort, heap_before);
            const uint32_t child            = m_storage.Heap[--ready];
            m_storage.LeafFirst[produced++] = child;
            for (uint32_t edge = m_storage.Heads[child]; edge != CookGraphInvalidIndex; edge = m_storage.Next[edge])
            {
                const uint32_t parent = m_storage.Parents[edge];
                if (!--m_storage.Pending[parent])
                {
                    m_storage.Heap[ready++] = parent;
                    Core::Containers::HeapPush<uint32_t>({m_storage.Heap, ready}, sort, heap_before);
                }
            }
        }
        if (produced != m_node_count)
        {
            m_state = State::Failed;
            if (auto* retained = diagnostics ? diagnostics->Emplace(CookStage::Discover, CookDiagnosticCode::DependencyCycle, VFS::VFSError::Corrupted) : nullptr)
            {
                std::fill_n(m_storage.Heads, m_node_count, CookGraphInvalidIndex);
                uint32_t node = 0, length = 0;
                while (!m_storage.Pending[m_storage.Canonical[node]])
                    ++node;
                node = m_storage.Canonical[node];
                while (m_storage.Heads[node] == CookGraphInvalidIndex)
                {
                    m_storage.Heads[node]         = length;
                    m_storage.LeafFirst[length++] = node;
                    for (const auto& edge : Dependencies(node))
                        if (m_storage.Pending[edge.Child])
                        {
                            node = edge.Child;
                            break;
                        }
                }
                const uint32_t start = m_storage.Heads[node];
                auto&          entry = *retained;
                (void) Describe(node, CookDiagnosticCode::DependencyCycle, "Validated asset dependencies contain a cycle.", &entry);
                entry.Underlying     = VFS::VFSError::Corrupted;
                const uint32_t count = length - start + 1;
                entry.ChainCount     = std::min(count, static_cast<uint32_t>(entry.Chain.size()));
                entry.ChainTruncated = count > entry.Chain.size();
                for (uint32_t i = 0; i < entry.ChainCount; ++i)
                    entry.Chain[i] = m_storage.Nodes[i == count - 1 ? node : m_storage.LeafFirst[start + i]].Identity;
                if (entry.ChainTruncated)
                    entry.Chain[entry.ChainCount - 1] = m_storage.Nodes[node].Identity;
            }
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        for (uint32_t i = 0; i < m_occurrence_count; ++i)
            m_storage.ByOccurrence[i] = i;
        Core::Containers::HeapSort<uint32_t>({m_storage.ByOccurrence, m_occurrence_count}, sort, [&](uint32_t a, uint32_t b) {
            const auto& left  = m_storage.Occurrences[a];
            const auto& right = m_storage.Occurrences[b];
            return left.Scene == right.Scene ? before(left.Node, right.Node) : left.Scene < right.Scene;
        });
        uint32_t roots = 0;
        for (uint32_t i = 0; i < m_occurrence_count; ++i)
        {
            const auto& occurrence = m_storage.Occurrences[m_storage.ByOccurrence[i]];
            auto&       scene      = m_storage.Scenes[occurrence.Scene];
            if (scene.RootCount && m_storage.Roots[roots - 1] == occurrence.Node)
                continue;
            if (!scene.RootCount)
                scene.FirstRoot = roots;
            m_storage.Roots[roots++] = occurrence.Node;
            ++scene.RootCount;
        }
        m_state = State::Finalized;
        return VFS::VFSResult<void>::Ok();
    }

    bool CookDependencyGraph::IsFinalized() const noexcept
    {
        return m_state == State::Finalized;
    }
    const CookGraphNode* CookDependencyGraph::Node(uint32_t index) const noexcept
    {
        return index < m_node_count ? m_storage.Nodes + index : nullptr;
    }
    const CookGraphScene* CookDependencyGraph::Scene(uint32_t index) const noexcept
    {
        return index < m_scene_count ? m_storage.Scenes + index : nullptr;
    }
    const CookGraphOccurrence* CookDependencyGraph::Occurrence(uint32_t index) const noexcept
    {
        return index < m_occurrence_count ? m_storage.Occurrences + index : nullptr;
    }

    Core::Containers::ArrayView<const CookGraphEdge> CookDependencyGraph::Dependencies(uint32_t index) const noexcept
    {
        const auto* node = Node(index);
        return node && node->EdgeCount ? Core::Containers::ArrayView<const CookGraphEdge>(m_storage.Edges + node->FirstEdge, node->EdgeCount) : Core::Containers::ArrayView<const CookGraphEdge>{};
    }
    Core::Containers::StringView CookDependencyGraph::Slot(const CookGraphEdge& edge) const noexcept
    {
        return edge.SlotBytes && edge.SlotOffset <= m_text_bytes && edge.SlotBytes < m_text_bytes - edge.SlotOffset ? Core::Containers::StringView(m_storage.Text + edge.SlotOffset, edge.SlotBytes) : Core::Containers::StringView{};
    }
    Core::Containers::StringView CookDependencyGraph::OriginalSource(const CookGraphEdge& edge) const noexcept
    {
        return edge.SourceBytes && edge.SourceOffset <= m_text_bytes && edge.SourceBytes < m_text_bytes - edge.SourceOffset ? Core::Containers::StringView(m_storage.Text + edge.SourceOffset, edge.SourceBytes) : Core::Containers::StringView{};
    }
    Core::Containers::ArrayView<const uint32_t> CookDependencyGraph::CanonicalNodes() const noexcept
    {
        return IsFinalized() ? Core::Containers::ArrayView<const uint32_t>(m_storage.Canonical, m_node_count) : Core::Containers::ArrayView<const uint32_t>{};
    }
    Core::Containers::ArrayView<const uint32_t> CookDependencyGraph::LeafFirstNodes() const noexcept
    {
        return IsFinalized() ? Core::Containers::ArrayView<const uint32_t>(m_storage.LeafFirst, m_node_count) : Core::Containers::ArrayView<const uint32_t>{};
    }
    Core::Containers::ArrayView<const uint32_t> CookDependencyGraph::SceneRoots(uint32_t index) const noexcept
    {
        const auto* scene = Scene(index);
        return IsFinalized() && scene && scene->RootCount ? Core::Containers::ArrayView<const uint32_t>(m_storage.Roots + scene->FirstRoot, scene->RootCount) : Core::Containers::ArrayView<const uint32_t>{};
    }

    VFS::VFSResult<void> CookDependencyGraph::SceneClosure(uint32_t scene, Core::Containers::ArrayView<uint32_t> workspace, Core::Containers::ArrayView<const uint32_t>* result) const
    {
        if (!IsFinalized() || scene >= m_scene_count || !result || (workspace.size() && !workspace.data()))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        if (workspace.size() < size_t(m_node_count) * 2)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        if (!m_node_count)
        {
            *result = {};
            return VFS::VFSResult<void>::Ok();
        }
        auto* marked = workspace.data();
        auto* stack  = marked + m_node_count;
        std::fill_n(marked, m_node_count, 0);
        uint32_t   count = 0;
        const auto push  = [&](uint32_t node) {
            if (!marked[node])
            {
                marked[node]   = 1;
                stack[count++] = node;
            }
        };
        for (uint32_t root : SceneRoots(scene))
            push(root);
        while (count)
            for (const auto& edge : Dependencies(stack[--count]))
                push(edge.Child);
        for (uint32_t node : CanonicalNodes())
            if (marked[node])
                stack[count++] = node;
        *result = {stack, count};
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> CookDependencyGraph::Describe(uint32_t index, CookDiagnosticCode code, cstring message, CookDiagnostic* output) const
    {
        const auto* node = Node(index);
        if (!node || !output || node->OriginOccurrence >= m_occurrence_count)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto& occurrence = m_storage.Occurrences[node->OriginOccurrence];
        if (occurrence.Scene >= m_scene_count || !ValidProvenance(occurrence.SchemaKey, occurrence.FieldKey))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        // SetMessage validates before writing and supports text borrowed from any
        // part of the old output. Do it before replacing provenance/chain fields.
        // All remaining assignments are infallible: graph-owned keys were checked
        // above and cannot alias the caller's record. No second full diagnostic
        // or aggregate-reset temporary is needed for transactional failure.
        if (!output->SetMessage(message))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto& scene = m_storage.Scenes[occurrence.Scene];
        // Occurrence keys are owned, zero-padded arrays initialized by AddRoot;
        // unlike public SetKeys, this copy needs no alias-preserving buffers.
        Helpers::secure_memcpy(output->SchemaKey, sizeof(output->SchemaKey), occurrence.SchemaKey, sizeof(occurrence.SchemaKey));
        Helpers::secure_memcpy(output->FieldKey, sizeof(output->FieldKey), occurrence.FieldKey, sizeof(occurrence.FieldKey));
        output->Severity   = CookSeverity::Error;
        output->Stage      = CookStage::Discover;
        output->Code       = code;
        output->Underlying = VFS::VFSError::OK;
        output->Source     = scene.Source;
        output->SceneUUID  = scene.UUID;
        output->EntityUUID = occurrence.EntityUUID;
        output->Asset      = node->Identity;
        uint32_t depth = 0, cursor = index;
        while (cursor != CookGraphInvalidIndex)
        {
            ++depth;
            cursor = m_storage.Nodes[cursor].FirstParent;
        }
        output->ChainCount     = std::min(depth, static_cast<uint32_t>(output->Chain.size()));
        output->ChainTruncated = depth > output->Chain.size();
        cursor                 = index;
        for (uint32_t skip = depth; skip > output->ChainCount; --skip)
            cursor = m_storage.Nodes[cursor].FirstParent;
        for (uint32_t i = output->ChainCount; i; --i)
        {
            output->Chain[i - 1] = m_storage.Nodes[cursor].Identity;
            cursor               = m_storage.Nodes[cursor].FirstParent;
        }
        if (output->ChainTruncated)
            output->Chain[output->ChainCount - 1] = node->Identity;
        for (size_t i = output->ChainCount; i < output->Chain.size(); ++i)
            output->Chain[i] = {};
        return VFS::VFSResult<void>::Ok();
    }

    void CookDependencyGraph::Reset() noexcept
    {
        if (m_storage.Block)
            m_allocator->Free(m_storage.Block);
        m_allocator  = nullptr;
        m_limits     = {};
        m_storage    = {};
        m_node_count = m_edge_count = m_scene_count = m_occurrence_count = m_text_bytes = 0;
        m_scene_reference_count                                                         = 0;
        m_state                                                                         = State::Empty;
    }
} // namespace ZEngine::Cook
