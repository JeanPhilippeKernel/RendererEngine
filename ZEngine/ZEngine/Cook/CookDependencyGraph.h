#pragma once
#include <ZEngine/Cook/CookAdapter.h>

namespace ZEngine::Cook
{
    inline constexpr uint32_t CookGraphInvalidIndex = UINT32_MAX;

    struct CookGraphNode
    {
        CookAssetIdentity Identity         = {};
        Digest256         LocalContentKey  = {};
        Digest256         MetadataKey      = {};
        cstring           Format           = nullptr;
        uint32_t          AdapterRevision  = 0;
        uint32_t          FirstEdge        = 0;
        uint32_t          EdgeCount        = 0;
        uint32_t          OriginOccurrence = CookGraphInvalidIndex;
        uint32_t          FirstParent      = CookGraphInvalidIndex;
        bool              Inspected        = false;
    };

    struct CookGraphEdge
    {
        uint32_t Child        = CookGraphInvalidIndex;
        uint32_t SlotOffset   = 0;
        uint32_t SlotBytes    = 0;
        uint32_t SourceOffset = 0;
        uint32_t SourceBytes  = 0;
    };

    struct CookGraphScene
    {
        uuids::uuid                UUID      = {};
        Core::VFS::VFSRelativePath Source    = {};
        uint32_t                   FirstRoot = 0;
        uint32_t                   RootCount = 0;
    };

    // Keep authored reference occurrences even when their semantic roots share
    // a node. No schema callback/string view survives its enumeration call.
    struct CookGraphOccurrence
    {
        uint32_t    Scene          = CookGraphInvalidIndex;
        uint32_t    Node           = CookGraphInvalidIndex;
        uuids::uuid EntityUUID     = {};
        char        SchemaKey[128] = {};
        char        FieldKey[128]  = {};
    };

    // Run-owned forward graph. One checked, geometrically grown block contains
    // compact records/text, identity hash index, and all finalization workspace.
    // Initialize reserves nothing. No per-node/edge allocation or recursive walk.
    // Numeric indexes survive growth; ALL borrowed pointers/views are invalidated
    // by a mutating call (including a failed call that grew retained capacity).
    // Failures preserve logical records/counts. Reset releases retained capacity.
    // Allocator outlives this object; count sizeof(this) in fixed run accounting.
    class CookDependencyGraph
    {
    public:
        CookDependencyGraph() = default;
        ~CookDependencyGraph();
        CookDependencyGraph(const CookDependencyGraph&)                                    = delete;
        CookDependencyGraph&                         operator=(const CookDependencyGraph&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void>     Initialize(ICookAllocator&, const CookLimits&);
        [[nodiscard]] Core::VFS::VFSResult<uint32_t> AddScene(const uuids::uuid&, const Core::VFS::VFSRelativePath&);
        // Enumerates a validated immutable document, including settings. Counts
        // all callbacks before filtering nil fallbacks/entity references, rejects
        // untyped assets, and copies provenance. Failure rolls back this scene's
        // logical records/counts, not retained capacity. No ECS world or parser.
        [[nodiscard]] Core::VFS::VFSResult<uint32_t> AddScene(const ECS::SceneDocument&, const ECS::SceneComponentSchemaRegistry&, const Core::VFS::VFSRelativePath&, CookDiagnostics*);
        // Non-nil, typed asset references only. Scene enumeration must enforce
        // its complete occurrence cap before filtering format-defined nils or
        // entity references; entity references remain scene validation's job.
        [[nodiscard]] Core::VFS::VFSResult<void>     AddRoot(uint32_t scene, const CookAssetIdentity&, const uuids::uuid& entity, Core::Containers::StringView schema, Core::Containers::StringView field);
        [[nodiscard]] Core::VFS::VFSResult<void>     BeginDiscovery();
        // Caller resolves UUID/path pairs against the active frozen catalog
        // BEFORE committing inspection. Every node is inspected exactly once.
        // Copies only semantic keys/dependencies/text, never decoded documents.
        [[nodiscard]] Core::VFS::VFSResult<void>     SetInspection(uint32_t node, uint32_t adapter_revision, const CookAssetInspection&);
        // Validated borrowed records (e.g. a manifest) use the same graph copy/
        // indexing/finalization path without allocating an owned inspection.
        [[nodiscard]] Core::VFS::VFSResult<void>     SetInspection(uint32_t node, uint32_t adapter_revision, const CookAssetInspectionView&);
        // Nonempty graphs reserve one shared constant sort/ready-queue state.
        // Reservation refusal precedes workspace mutation and permits retry.
        [[nodiscard]] Core::VFS::VFSResult<void>     Finalize(CookDiagnostics*);
        void                                         Reset() noexcept;

        [[nodiscard]] uint32_t                       NodeCount() const noexcept
        {
            return m_node_count;
        }
        [[nodiscard]] uint32_t EdgeCount() const noexcept
        {
            return m_edge_count;
        }
        [[nodiscard]] uint32_t SceneCount() const noexcept
        {
            return m_scene_count;
        }
        [[nodiscard]] uint32_t OccurrenceCount() const noexcept
        {
            return m_occurrence_count;
        }
        [[nodiscard]] uint32_t ReferenceCount() const noexcept
        {
            return m_scene_reference_count + m_edge_count;
        }
        [[nodiscard]] bool                                             IsFinalized() const noexcept;
        [[nodiscard]] uint32_t                                         Find(const CookAssetIdentity&) const noexcept;
        [[nodiscard]] const CookGraphNode*                             Node(uint32_t) const noexcept;
        [[nodiscard]] const CookGraphScene*                            Scene(uint32_t) const noexcept;
        [[nodiscard]] const CookGraphOccurrence*                       Occurrence(uint32_t) const noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const CookGraphEdge> Dependencies(uint32_t) const noexcept;
        [[nodiscard]] Core::Containers::StringView                     Slot(const CookGraphEdge&) const noexcept;
        [[nodiscard]] Core::Containers::StringView                     OriginalSource(const CookGraphEdge&) const noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const uint32_t>      CanonicalNodes() const noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const uint32_t>      LeafFirstNodes() const noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const uint32_t>      SceneRoots(uint32_t) const noexcept;
        // 2 * NodeCount uint32_t words supplied/charged by caller. Result borrows
        // that workspace, is canonical, and survives until workspace reuse. No
        // mutation of the finalized graph; failure preserves the old result view.
        [[nodiscard]] Core::VFS::VFSResult<void>                       SceneClosure(uint32_t scene, Core::Containers::ArrayView<uint32_t> workspace, Core::Containers::ArrayView<const uint32_t>* result) const;
        // First-discovery provenance tree is acyclic even for cyclic semantic
        // graphs. Bounded root-first chain and original scene/entity/field scope.
        // No allocation or second diagnostic record. Failure preserves output;
        // message may borrow text from the old output, including its key buffers.
        [[nodiscard]] Core::VFS::VFSResult<void>                       Describe(uint32_t node, CookDiagnosticCode, cstring message, CookDiagnostic*) const;

    private:
        enum class State : uint8_t
        {
            Empty = 0,
            Roots,
            Discovering,
            Finalized,
            Failed
        };
        struct Storage
        {
            void*                Block              = nullptr;
            CookGraphNode*       Nodes              = nullptr;
            CookGraphEdge*       Edges              = nullptr;
            CookGraphScene*      Scenes             = nullptr;
            CookGraphOccurrence* Occurrences        = nullptr;
            char*                Text               = nullptr;
            uint32_t*            Hash               = nullptr;
            uint32_t*            Canonical          = nullptr;
            uint32_t*            LeafFirst          = nullptr;
            uint32_t*            Pending            = nullptr;
            uint32_t*            Heads              = nullptr;
            uint32_t*            Heap               = nullptr;
            uint32_t*            Parents            = nullptr;
            uint32_t*            Next               = nullptr;
            uint32_t*            ByOccurrence       = nullptr;
            uint32_t*            Roots              = nullptr;
            uint32_t             NodeCapacity       = 0;
            uint32_t             EdgeCapacity       = 0;
            uint32_t             SceneCapacity      = 0;
            uint32_t             OccurrenceCapacity = 0;
            uint32_t             TextCapacity       = 0;
            uint32_t             HashCapacity       = 0;
        };
        [[nodiscard]] Core::VFS::VFSResult<void>     Ensure(uint32_t nodes, uint32_t edges, uint32_t scenes, uint32_t occurrences, uint32_t text);
        [[nodiscard]] Core::VFS::VFSResult<uint32_t> Intern(const CookAssetIdentity&, uint32_t origin, uint32_t parent);
        void                                         Reindex() noexcept;
        void                                         Index(uint32_t) noexcept;
        [[nodiscard]] uint32_t                       StoreText(Core::Containers::StringView);

        ICookAllocator*                              m_allocator             = nullptr;
        CookLimits                                   m_limits                = {};
        Storage                                      m_storage               = {};
        uint32_t                                     m_node_count            = 0;
        uint32_t                                     m_edge_count            = 0;
        uint32_t                                     m_scene_count           = 0;
        uint32_t                                     m_occurrence_count      = 0;
        uint32_t                                     m_scene_reference_count = 0;
        uint32_t                                     m_text_bytes            = 0;
        State                                        m_state                 = State::Empty;
    };
} // namespace ZEngine::Cook
