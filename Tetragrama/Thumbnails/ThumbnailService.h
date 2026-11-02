#pragma once
#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Rendering/Materials/MaterialAlpha.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/ZEngineDef.h>
#include <atomic>

namespace ZEngine::Managers
{
    struct AssetManager;
}

namespace ZEngine::Rendering::Renderers
{
    struct AssetPreviewPass;
}

namespace Tetragrama::Thumbnails
{
    struct ThumbnailServiceTestAccess;

    /// @brief Bounded visible-asset preview cache owned by the editor.
    /// @details Produces 128px texture previews through RRM workers and mesh/material previews
    ///          through the render graph. The editor thread owns cache state. Asset registry
    ///          callbacks only enqueue invalidations for Update() to apply.
    struct ThumbnailService
    {
        void                                        Initialize(ZEngine::Core::VFS::AssetRegistry* registry, ZEngine::Rendering::RenderResourceManager* resource_manager, cstring project_root, ZEngine::Rendering::Renderers::AssetPreviewPass* previews = nullptr, ZEngine::Core::VFS::IVFSContext* vfs = nullptr);
        /// Worker pool must be joined before deinitialization/reinitialization.
        void                                        Deinitialize();

        /// @brief Returns a resident texture thumbnail or an invalid handle while work is pending.
        ZEngine::Rendering::Textures::TextureHandle Acquire(const ZEngine::Core::VFS::AssetRecordSnapshot& record, bool* fully_transparent = nullptr, ZEngine::Rendering::Materials::AlphaInfo* alpha_info = nullptr);

        /// @brief Polls decode completion and retires invalidated requests.
        void                                        Update();

        struct Statistics
        {
            uint32_t Resident           = 0;
            uint32_t FullyTransparent   = 0;
            uint32_t Queued             = 0;
            uint32_t InFlight           = 0;
            uint32_t Failed             = 0;
            uint32_t AssetLoads         = 0;
            uint32_t SubmittedThisFrame = 0;
            uint64_t Evictions          = 0;
            uint64_t BudgetDeferrals    = 0;
            uint64_t ResidentPixelBytes = 0;
        };
        [[nodiscard]] Statistics GetStatistics() const;
        [[nodiscard]] bool       IsFileDecoded(const ZEngine::Core::VFS::AssetRecordSnapshot& record) const;

    private:
        friend struct ThumbnailServiceTestAccess;

        enum class State : uint8_t
        {
            Empty = 0,
            Pending,
            Resident,
            FullyTransparent,
            Failed,
            Discarded,
        };

        struct Entry
        {
            uuids::uuid                                 AssetUUID      = {};
            uint64_t                                    SourceHash     = 0;
            uint64_t                                    DependencyHash = 0;
            uint64_t                                    PersistentHash = 0;
            ZEngine::Managers::AssetType                Kind           = ZEngine::Managers::AssetType::TEXTURE;
            ZEngine::Rendering::Textures::TextureHandle Texture        = {};
            uint64_t                                    LastUseFrame   = 0;
            uint64_t                                    NextRetryFrame = 0;
            State                                       CurrentState   = State::Empty;
            uint8_t                                     FailureCount   = 0;
            bool                                        GraphRequest   = false;
            bool                                        CacheMiss      = false;
        };

        struct Invalidation
        {
            uuids::uuid AssetUUID = {};
        };

        struct AssetLoad
        {
            ZEngine::Managers::AssetManager*        Assets    = nullptr;
            ZEngine::Core::VFS::IVFSContext*        VFS       = nullptr;
            ZEngine::Core::VFS::AssetRecordSnapshot Record    = {};
            ZEngine::Core::Memory::ArenaAllocator   Scratch   = {};
            std::atomic<bool>                       Cancelled = false;
            std::atomic<bool>                       Done      = false;
            bool                                    Succeeded = false;
            bool                                    Active    = false; // Editor-owned; worker never reads or writes it.
            static void                             Run(void* context);
            bool                                    Load();
        };
        // Separate leases survive entry eviction/cancellation until their workers
        // finish. Navigation cannot grow the number of concurrent CPU decodes.
        static constexpr uint32_t        kMaxAssetLoads = 4;
        AssetLoad                        m_asset_loads[kMaxAssetLoads];
        ZEngine::Core::VFS::IVFSContext* m_vfs = nullptr;
        static bool                      SubmitAssetLoad(void*, void*, void (*)(void*));
        bool                             EnsureAssetLoaded(const ZEngine::Core::VFS::AssetRecordSnapshot&);
        void                             PollAssetLoads();
        void                             CancelAssetLoads(const uuids::uuid&);
        void*                            m_load_submit_context                                          = nullptr;
        bool (*m_submit_asset_load)(void*, void*, void (*)(void*))                                      = &SubmitAssetLoad;

        static constexpr uint32_t                                                 kCapacity             = 512;
        static constexpr uint32_t                                                 kInvalidationCapacity = 64;
        static constexpr uint32_t                                                 kThumbnailExtent      = 128;
        static constexpr uint32_t                                                 kSchemaRevision       = 3;
        static constexpr uint32_t                                                 kSubmissionsPerFrame  = 4;
        static constexpr uint32_t                                                 kMaxInFlight          = 16;

        static void                                                               OnAssetStale(void* context, const uuids::uuid& uuid);
        static void                                                               OnAssetRemoved(void* context, const uuids::uuid& uuid, ZEngine::Managers::AssetType type);
        static bool                                                               IsPreviewable(const ZEngine::Core::VFS::AssetRecordSnapshot& record);

        Entry*                                                                    Find(const uuids::uuid& uuid, uint64_t source_hash);
        Entry*                                                                    PrepareRequest(const uuids::uuid& uuid, uint64_t source_hash);
        Entry*                                                                    Allocate(const uuids::uuid& uuid, uint64_t source_hash);
        bool                                                                      TakeSubmissionBudget();
        bool                                                                      ReclaimForResourcePressure();
        bool                                                                      BuildCachePath(const uuids::uuid& uuid, uint64_t source_hash, char (&out_path)[MAX_FILE_PATH_COUNT], uint64_t dependency_hash = 0, uint32_t renderer_revision = 0) const;
        void                                                                      QueueInvalidation(const uuids::uuid& uuid);
        void                                                                      Invalidate(const uuids::uuid& uuid);
        void                                                                      InvalidateAll();
        void                                                                      MarkFailed(Entry& entry);
        void                                                                      ReleaseAndClear(Entry& entry);

        ZEngine::Core::VFS::AssetRegistry*                                        m_registry                          = nullptr;
        ZEngine::Rendering::RenderResourceManager*                                m_resource_manager                  = nullptr;
        ZEngine::Rendering::Renderers::AssetPreviewPass*                          m_previews                          = nullptr;
        char                                                                      m_project_root[MAX_FILE_PATH_COUNT] = {};
        Entry                                                                     m_entries[kCapacity]                = {};
        ZEngine::Core::Containers::MPSCQueue<Invalidation, kInvalidationCapacity> m_invalidations                     = {};
        PaddedAtomic<bool>                                                        m_invalidation_overflow             = {};
        uint64_t                                                                  m_frame                             = 0;
        uint64_t                                                                  m_resource_retry_frame              = 0;
        uint64_t                                                                  m_next_cleanup_frame                = 60;
        uint32_t                                                                  m_submitted_this_frame              = 0;
        uint64_t                                                                  m_evictions                         = 0;
        uint64_t                                                                  m_budget_deferrals                  = 0;
    };
} // namespace Tetragrama::Thumbnails
