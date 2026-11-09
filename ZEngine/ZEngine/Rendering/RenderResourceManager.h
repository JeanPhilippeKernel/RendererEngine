#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/ECS/SceneLoadCoordinator.h>
#include <ZEngine/Hardwares/AsyncUploadQueue.h>
#include <ZEngine/Hardwares/CommandBufferManager.h>
#include <ZEngine/Hardwares/DeferredFreeQueue.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/EnvironmentLighting.h>
#include <ZEngine/Rendering/GeometryPool.h>
#include <ZEngine/Rendering/GeometryStreamingManager.h>
#include <ZEngine/Rendering/Pools/CommandPool.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <ZEngine/Rendering/RenderHandle.h>
#include <ZEngine/Rendering/SceneTextureDescriptorPreparation.h>
#include <ZEngine/Rendering/Scenes/PreparedSceneFrame.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <mutex>
#include <span>

// Forward declaration for the test-only helper that accesses RRM private members.
struct RRMTestHelper;
struct RRMDecodeTestHelper;
struct RRMUploadBatchTestHelper;

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
    struct CommandBuffer;
} // namespace ZEngine::Hardwares

namespace ZEngine::Rendering::Primitives
{
    struct Fence;
} // namespace ZEngine::Rendering::Primitives

namespace ZEngine::Rendering
{
    // RenderResourceManager — single authority over GPU buffer/image lifetime.
    //
    // Sits on EngineContext between the asset layer (AssetRegistry, AssetManager)
    // and the GPU layer (VulkanDevice, GpuAllocator). Neither layer knows about
    // the other; all coupling flows through here.
    //
    // THREAD SAFETY:
    //   Initialize / Shutdown       — main thread, once at startup/teardown
    //   BeginFrame                  — render thread only
    //   OnAssetReady callback       — asset/import thread; mesh only (protected by m_pending_mutex)
    //   OnAssetStale callback       — asset/import thread; mesh only, ScheduleSwap protected
    //                                 by m_pending_swap_mutex. Textures are triggered via
    //                                 TextureImporter/ImportCoordinator, not this callback.
    //   OnAssetRemoved callback     — asset/import thread; textures only, via ReleaseTexture
    //   ScheduleTextureReload       — any thread; protected by m_pending_texture_reload_mutex
    //   ReleaseMeshGeometry         — any thread; queues an identity captured under the UUID-map lock
    //   GetBuffer / GetTexture      — render thread read; asset thread writes via pending queues
    class RenderResourceManager
    {
    public:
        /// Immutable draw layout captured from the same CPU mesh as the upload.
        /// Material UUIDs are identities, not retained material/texture versions.
        struct MeshDrawMetadata
        {
            std::span<const Importers::AssetSubMesh> SubMeshes    = {};
            Core::Maths::Vec3f                       BoundsCenter = {};
            float                                    BoundsRadius = 0.f;
            Core::Maths::Vec3f                       BoundsMin    = {};
            Core::Maths::Vec3f                       BoundsMax    = {};
            bool                                     BoundsValid  = false;

        private:
            friend class RenderResourceManager;
            Core::Memory::TLSFSlab* Slab = nullptr;
        };

        /// Borrowed from a retained geometry pin, including its matching CPU draw
        /// metadata. No material/texture/descriptor/pipeline version is retained.
        struct MeshGeometrySnapshot
        {
            GeometryRegion          Region       = {};
            uint64_t                Version      = 0;
            uint32_t                VertexCount  = 0;
            uint32_t                IndexCount   = 0;
            const MeshDrawMetadata* DrawMetadata = nullptr;

            uint32_t                VertexOffset() const
            {
                return static_cast<uint32_t>(Region.VtxByteOffset / DRAW_VERTEX_BYTES);
            }
            uint32_t IndexOffset() const
            {
                return static_cast<uint32_t>(Region.IdxByteOffset / sizeof(uint32_t));
            }
        };

        /// Render-thread-only lease. The manager must outlive every pin, and the
        /// caller must retain it until its CPU and submitted GPU consumers retire.
        /// A pin preserves bytes/offsets; it does not prove upload completion.
        class MeshGeometryPin
        {
        public:
            MeshGeometryPin() = default;
            ~MeshGeometryPin();
            MeshGeometryPin(const MeshGeometryPin&)            = delete;
            MeshGeometryPin& operator=(const MeshGeometryPin&) = delete;
            MeshGeometryPin(MeshGeometryPin&& other) noexcept;
            MeshGeometryPin&            operator=(MeshGeometryPin&& other) noexcept;

            const MeshGeometrySnapshot* Get() const;
            explicit                    operator bool() const
            {
                return Get() != nullptr;
            }
            void Reset();

        private:
            friend class RenderResourceManager;
            MeshGeometryPin(RenderResourceManager* owner, uint32_t record, uint64_t version);
            RenderResourceManager* m_owner   = nullptr;
            uint32_t               m_record  = UINT32_MAX;
            uint64_t               m_version = 0;
        };

        static constexpr uint32_t TextureVersionCapacity = 8192;
        static constexpr uint32_t TextureRetirementSlice = 16;

        struct TextureVersionSnapshot
        {
            Textures::TextureHandle                 Handle  = {};
            Helpers::Handle<Hardwares::ImageBuffer> Image   = {};
            uint64_t                                Version = 0;
            uint32_t                                Width   = 0;
            uint32_t                                Height  = 0;
            Specifications::ImageFormat             Format  = Specifications::ImageFormat::UNDEFINED;
        };

        /// Render-thread-only immutable asset-texture lease. Retain through the
        /// last CPU/GPU consumer. A pin is not proof of upload/descriptor readiness.
        class TextureVersionPin
        {
        public:
            TextureVersionPin() = default;
            ~TextureVersionPin();
            TextureVersionPin(const TextureVersionPin&)            = delete;
            TextureVersionPin& operator=(const TextureVersionPin&) = delete;
            TextureVersionPin(TextureVersionPin&& other) noexcept;
            TextureVersionPin&            operator=(TextureVersionPin&& other) noexcept;
            const TextureVersionSnapshot* Get() const;
            explicit                      operator bool() const
            {
                return Get() != nullptr;
            }
            void Reset();

        private:
            friend class RenderResourceManager;
            RenderResourceManager* m_owner   = nullptr;
            uint32_t               m_slot    = UINT32_MAX;
            uint64_t               m_version = 0;
        };

        struct MaterialVersionSnapshot
        {
            Meshes::MeshMaterial Material = {};
            uint64_t             Version  = 0;
        };

        /// Caller-owned fixed storage: copied shader values plus up to five
        /// retained texture versions. Scene preparation adds exact shared-bindless
        /// publication proofs; clones carry them through frame retirement.
        /// Missing required maps fail without fallback.
        class MaterialVersionPin
        {
        public:
            MaterialVersionPin()                                     = default;
            MaterialVersionPin(const MaterialVersionPin&)            = delete;
            MaterialVersionPin& operator=(const MaterialVersionPin&) = delete;
            MaterialVersionPin(MaterialVersionPin&& other) noexcept;
            MaterialVersionPin&            operator=(MaterialVersionPin&& other) noexcept;
            const MaterialVersionSnapshot* Get() const
            {
                return m_snapshot.Version ? &m_snapshot : nullptr;
            }
            const TextureVersionSnapshot* GetTexture(uint32_t map) const
            {
                return map < 5 ? m_textures[map].Get() : nullptr;
            }
            explicit operator bool() const
            {
                return Get() != nullptr;
            }
            void Reset();

        private:
            friend class RenderResourceManager;
            MaterialVersionSnapshot m_snapshot               = {};
            TextureVersionPin       m_textures[5]            = {};
            uint64_t                m_descriptor_versions[5] = {};
            bool                    m_descriptors_required   = false;
        };

        enum class MaterialVersionPolicy : uint8_t
        {
            ExistingBindings = 0,
            RequiredSceneBindings,
        };
        enum class MaterialVersionError : uint8_t
        {
            None = 0,
            MissingMaterial,
            MissingRequiredTexture,
            DebugFallback,
            TextureIdentityMismatch,
            TextureLoadFailed,
            ResourcesUnavailable,
        };

        TextureVersionPin         PinTextureVersion(Textures::TextureHandle handle);
        /// Retains a graph-owned image identity and its shared allocation, not
        /// its contents. Graph barriers still order writes and alias reuse.
        TextureVersionPin         PinGraphTextureVersion(Textures::TextureHandle handle);
        /// Extend an existing exact texture lease into accepted-submission
        /// retirement. Does not reacquire a UUID or reopen sealed admission.
        /// Output is unchanged on refusal; the render owner calls this before
        /// recording a consumer, including pass-local sampled descriptors.
        bool                      GetFrameTextureVersion(const TextureVersionPin& retained, TextureVersionSnapshot* snapshot);
        TextureVersionPin         RetainTextureVersion(const TextureVersionPin& retained);
        /// RequiredSceneBindings validates authored texture identities under the
        /// cache lock and rejects debug fallbacks before retaining any GPU map.
        MaterialVersionPin        PinMaterialVersion(const uuids::uuid& uuid, MaterialVersionPolicy policy = MaterialVersionPolicy::ExistingBindings, MaterialVersionError* error = nullptr);
        static constexpr uint32_t FrameMaterialCapacity = 4096;
        /// Render-thread draw admission. The returned table index and copied
        /// shader values refer to one retained version, shared across draws/frames.
        /// Legacy scene callers explicitly opt into the cache's fallback material.
        bool                      GetFrameMaterialVersion(const uuids::uuid& uuid, uint32_t* index, MaterialVersionSnapshot* snapshot, bool use_fallback = false);
        bool                      GetFrameMaterialVersion(const Meshes::MeshMaterial& material, uint32_t* index, MaterialVersionSnapshot* snapshot);
        /// Extend an existing lease into automatic frame retirement without
        /// reacquiring the mutable cache or reopening sealed texture admission.
        bool                      GetFrameMaterialVersion(const MaterialVersionPin& retained, uint32_t* index, MaterialVersionSnapshot* snapshot);
        /// Upload only admitted immutable entries, never the mutable asset cache.
        void                      UploadFrameMaterials(Core::Memory::BufferView& destination);
        /// Device mutation/retirement guards; callable by producer threads.
        bool                      IsTextureVersionPinned(Textures::TextureHandle handle) const;
        bool                      CanWriteTextureVersion(Textures::TextureHandle handle) const;
        /// Managed writers observe producer completion, require acknowledged
        /// ownership and invalidate previous admission proof before changing bytes.
        bool                      BeginTextureVersionWrite(Textures::TextureHandle handle);
        /// force_defer preserves retirement when the bounded native queue refuses admission.
        bool                      RetainTextureForRetirement(Textures::TextureHandle handle, bool force_defer = false);

        static constexpr uint32_t SceneResourcePreparationCapacity = 2;
        static constexpr uint32_t SceneGeometryMeshCapacity        = 4096;
        static constexpr uint32_t SceneResourceSliceOperations     = 16;
        static constexpr uint32_t SceneMaterialCapacity            = 4096;
        static constexpr uint32_t SceneDrawCapacity                = Scenes::SceneData::MAX_DRAW_COMMANDS;

        /// Main-producer input copied into fixed request storage at admission.
        /// The span is never borrowed by render-owner preparation.
        struct SceneInitialFrameInput
        {
            std::span<const Scenes::MeshInstance> Instances = {};
            Scenes::LightArrayUBO                 Lights    = {};
            Scenes::SceneGridRenderSnapshot       Grid      = {};
            Scenes::SceneInitialView              View      = {};
        };

        enum class ScenePreparationContent : uint8_t
        {
            GeometryOnly,
            MeshMaterials
        };

        struct SceneResourcePreparationId
        {
            ECS::SceneRequestId Request                                             = {};
            uint64_t            Serial                                              = 0;
            uint32_t            Slot                                                = 0;
            bool                operator==(const SceneResourcePreparationId&) const = default;
            explicit            operator bool() const noexcept
            {
                return bool(Request) && Serial != 0;
            }
        };
        /// Main-producer acquired; may be moved/released on the render consumer.
        /// Keeps request storage and its exact bindings alive across cancellation.
        class SceneResourceLease final
        {
        public:
            SceneResourceLease() = default;
            ~SceneResourceLease();
            SceneResourceLease(const SceneResourceLease&)            = delete;
            SceneResourceLease& operator=(const SceneResourceLease&) = delete;
            SceneResourceLease(SceneResourceLease&& other) noexcept;
            SceneResourceLease& operator=(SceneResourceLease&& other) noexcept;
            explicit            operator bool() const noexcept
            {
                return m_owner != nullptr;
            }
            void Reset() noexcept;

        private:
            friend class RenderResourceManager;
            RenderResourceManager*     m_owner = nullptr;
            SceneResourcePreparationId m_id    = {};
        };
        enum class SceneResourcePreparationState : uint8_t
        {
            Preparing,
            GeometryReady,
            ResourcesRetained,
            Failed,
            Releasing
        };
        enum class SceneResourcePreparationError : uint8_t
        {
            None,
            MissingGeometry,
            GeometryCapacity,
            NotResident,
            PinCapacity,
            UploadFailed,
            MissingDrawMetadata,
            MaterialCapacity,
            MaterialNotRetainable,
            RequiredMaterialBinding,
            RequiredEnvironment,
            RequiredPipeline,
            RequiredDescriptor,
            InvalidInitialFrame,
            DrawCapacity,
            InitialBufferAllocation,
            InitialBufferWrite,
        };
        enum class SceneEnvironmentPreparationState : uint8_t
        {
            NotRequested = 0,
            Queued,
            Preparing,
            Ready,
            Activated,
            Failed,
        };
        struct SceneEnvironmentInput
        {
            Scenes::SkyConfig         Sky            = {};
            Scenes::SkyCelestialLight CelestialLight = {};
            uint64_t                  SkyRevision    = 0;
        };
        struct SceneEnvironmentPreparation
        {
            SceneResourcePreparationId       Id                 = {};
            SceneEnvironmentInput            Input              = {};
            uint64_t                         ExpectedGeneration = 0;
            SceneEnvironmentPreparationState State              = SceneEnvironmentPreparationState::NotRequested;
            bool                             Cancelled          = false;
        };
        struct SceneResourcePreparationResult
        {
            SceneResourcePreparationId       Id                     = {};
            uint64_t                         ExpectedGeneration     = 0;
            uint32_t                         MeshCount              = 0;
            SceneResourcePreparationState    State                  = SceneResourcePreparationState::Preparing;
            SceneResourcePreparationError    Error                  = SceneResourcePreparationError::None;
            MaterialVersionError             MaterialError          = MaterialVersionError::None;
            ScenePreparationContent          Content                = ScenePreparationContent::GeometryOnly;
            // Published only at GeometryReady/ResourcesRetained or Failed.
            uint32_t                         MaterialCount          = 0;
            bool                             FrameRecorded          = false;
            SceneEnvironmentPreparationState Environment            = SceneEnvironmentPreparationState::NotRequested;
            bool                             InitialDrawsPrepared   = false;
            bool                             InitialBuffersPrepared = false;
            uint32_t                         InitialInstanceCount   = 0;
            uint32_t                         InitialDrawCount       = 0;
        };

        /// Single main-thread producer. Copies sorted, unique, non-nil UUIDs into
        /// fixed storage. Request identities come from SceneLoads and must never
        /// be reused. Admission failure changes neither out nor earlier work.
        /// MeshMaterials additionally traverses the retained draw metadata and
        /// restores cached mesh residency, and retains unique material values and
        /// their texture versions. An optional copied environment input also
        /// gates readiness on render-owner preparation and exact-frame activation.
        /// MeshMaterials also requires exact shared-bindless image publications.
        /// No absent mesh/material source decode, fallback, pass-local descriptor
        /// preparation or full visibility proof is provided. An environment request
        /// additionally gates the host-declared pipeline variants.
        /// Optional initial-frame input is copied before queuing, then expanded
        /// from retained metadata/material reservations under the same slice
        /// budget. Draw overflow/invalid ranges fail before resource readiness.
        /// GeometryOnly preserves the narrow upload-readiness contract.
        bool                          PrepareSceneResources(ECS::SceneRequestId request, uint64_t expected_generation, std::span<const uuids::uuid> meshes, SceneResourcePreparationId* out, ScenePreparationContent content = ScenePreparationContent::GeometryOnly, const SceneEnvironmentInput* environment = nullptr, const SceneInitialFrameInput* initial_frame = nullptr, bool defer_instances = false);
        /// Producer-thread only, serialized with admission/release. Deferred input
        /// is copied in batches of at most 64; no producer pointer is retained.
        /// Rendering cannot observe partial input. Invalid batches fail the request.
        bool                          AppendSceneResourceInstances(SceneResourcePreparationId id, std::span<const Scenes::MeshInstance> instances);
        bool                          PollSceneResourcePreparation(SceneResourcePreparationId id, SceneResourcePreparationResult* out) const;
        SceneResourceLease            AcquireSceneResourceLease(SceneResourcePreparationId id, uint64_t scene_generation);
        void                          NotifyPreparedSceneFrameRecorded(SceneResourcePreparationId id);
        /// Cancellation/release is asynchronous. Pins and bounded storage remain
        /// occupied until render-thread cleanup and accepted uploads retire.
        bool                          ReleaseSceneResourcePreparation(SceneResourcePreparationId id);
        /// Render-thread-only borrowed snapshot. Keep the request retained through
        /// every consumer, or transfer lifetime to a separate consumer pin.
        bool                          GetPreparedSceneGeometry(SceneResourcePreparationId id, const uuids::uuid& mesh, MeshGeometrySnapshot* out) const;
        /// Render-thread-only borrow with the same lifetime as prepared geometry.
        /// Use the retained values (not a new UUID lookup) for consumer admission.
        bool                          GetPreparedSceneMaterial(SceneResourcePreparationId id, const uuids::uuid& material, MaterialVersionSnapshot* out) const;
        /// Render-owner transfer of exact prepared versions into the normal
        /// recording/submission gates. After successful transfer, the request
        /// can be released without invalidating these admitted frame consumers.
        bool                          GetFramePreparedSceneGeometry(SceneResourcePreparationId id, const uuids::uuid& mesh, MeshGeometrySnapshot* out);
        bool                          GetFramePreparedSceneMaterial(SceneResourcePreparationId id, const uuids::uuid& material, uint32_t* index, MaterialVersionSnapshot* out);
        /// Camera-independent initial draw layout, available only before first
        /// recorded acknowledgement. False leaves out unchanged. The frame
        /// variant additionally retains every exact GPU dependency for retirement.
        bool                          GetPreparedSceneFrame(SceneResourcePreparationId id, Scenes::PreparedSceneFrame* out) const;
        bool                          GetFramePreparedSceneFrame(SceneResourcePreparationId id, Scenes::PreparedSceneFrame* out);
        /// Nonblocking render-owner pump; also used when frame acquire is skipped.
        void                          PollSceneResourcePreparations();
        /// Render-owner-only environment handoff. The lease prevents cancellation
        /// from recycling request storage while source/GPU work is still draining.
        /// Activate requires both the render-owner claim and a retained frame.
        SceneResourceLease            ClaimSceneEnvironmentPreparation(SceneEnvironmentPreparation* out);
        bool                          GetSceneEnvironmentPreparation(SceneResourcePreparationId id, SceneEnvironmentPreparation* out) const;
        bool                          CompleteSceneEnvironmentPreparation(SceneResourcePreparationId id, bool success, SceneResourcePreparationError error = SceneResourcePreparationError::RequiredEnvironment);
        bool                          ActivateSceneEnvironmentPreparation(SceneResourcePreparationId id);

        static constexpr uint32_t     FRAMES_IN_FLIGHT    = 3;
        static constexpr VkDeviceSize GLOBAL_VTX_CAPACITY = 512ULL * 1024 * 1024; // upper-bound clamp for DeriveGeometryBudget
        static constexpr VkDeviceSize GLOBAL_VTX_MIN      = 128ULL * 1024 * 1024; // lower-bound clamp

        /// @brief Streaming lifecycle state of a mesh slot.
        enum class StreamingState : uint8_t
        {
            Unloaded = 0, ///< No region allocated; never loaded or previously evicted.
            Pending,      ///< A deduplicated reload is queued; no drawable region yet.
            Resident,     ///< GPU-resident — safe to emit draw commands.
            Evicting,     ///< Marked for eviction; replacement upload may be in flight.
        };

        /// @brief Decode state exposed to render-thread clients that own a streamed texture's lifecycle.
        enum class TextureDecodeState : uint8_t
        {
            Pending = 0,
            Succeeded,
            Failed,
            Untracked,
        };

        /// @brief Initialize the RRM and bind it to a VulkanDevice and AssetRegistry.
        /// @details Registers OnAssetReady and OnAssetStale callbacks on the registry,
        ///          allocates the dedicated upload command pool and fence, and creates
        ///          the packed global vertex and index buffers.
        /// @param device   The active Vulkan device; must outlive this RRM instance.
        /// @param registry The asset registry to subscribe to; must outlive this RRM instance.
        /// @param upload_arena Owns bounded CPU decode storage and must outlive this
        ///                     manager and the worker-pool registrations.
        void                                Initialize(Hardwares::VulkanDevice* device, Core::VFS::AssetRegistry* registry, Core::Memory::ArenaAllocator* upload_arena);

        /// @brief Drain in-flight GPU work and release all GPU resources.
        /// @details Calls vkQueueWaitAll, shuts down texture timelines, and frees the
        ///          global geometry buffers and all generic buffer slots. Must be called
        ///          from the main thread before VulkanDevice teardown.
        void                                Shutdown();

        /// @brief Per-frame render-thread entry point.
        /// @details Flushes pending uploads, swaps, and texture reloads/releases queued
        ///          from other threads since the previous BeginFrame. Called by AppRenderPipeline.
        /// @param frame_index Current swapchain frame index (0 .. FRAMES_IN_FLIGHT-1).
        void                                BeginFrame(uint32_t frame_index);

        /// @brief Closes the current deferred upload batch, if anything joined it.
        /// @details Called by RenderGraph after its declaration phase and by
        ///          AppRenderPipeline::EndFrame as an idempotent fallback.
        void                                EndFrame();

        /// @brief Ingest a texture file, uploading (existing invalid) or reloading it in
        ///        place (existing valid) under the same handle.
        /// @param uuid          Asset UUID, used for logging only.
        /// @param absolute_path Absolute filesystem path to the image file.
        /// @param existing      Current handle when reimporting; invalid for first ingest.
        /// @return The texture's handle — same as existing when reimporting.
        Rendering::Textures::TextureHandle  IngestTexture(const uuids::uuid& uuid, const char* absolute_path, Rendering::Textures::TextureHandle existing = {});

        /// @brief Schedule a hot-reload reimport for a texture, deduped by UUID.
        /// @details Thread-safe; applied by the next FlushPendingTextureReloads.
        void                                ScheduleTextureReload(const uuids::uuid& uuid);

        /// Render-thread readiness guard for material-preview rendering. A valid
        /// bindless handle alone does not mean its asynchronous source decode ran.
        bool                                HasPendingTextureUploads();

        /// @brief Release a texture: patch referencing materials to the sentinel, then
        ///        timeline-gate the GPU-side teardown.
        /// @details Thread-safe. The CPU-side patch always lands before the bindless slot
        ///          can be reused — see Device->DestroyTexture for the GPU-side gate.
        void                                ReleaseTexture(const uuids::uuid& uuid);

        /// @brief Look up a texture by handle.
        /// @return Pointer to the Texture, or nullptr if the handle is invalid or stale.
        const Rendering::Textures::Texture* GetTexture(const Rendering::Textures::TextureHandle& handle) const;

        /// @brief Upload raw RGBA pixel data to an existing TextureHandle via the timeline path.
        /// @details Records a staging copy into an instant command buffer and enqueues the
        ///          submission to the async upload queue. Actual GPU submission drains in
        ///          SubmitAsyncUploads(). For font atlas upload prefer UploadFontAtlas.
        /// @param frame_index  Render frame index used to select the per-frame command pool.
        /// @param thread_index Thread index within the pool.
        /// @param handle       Pre-allocated TextureHandle whose VkImage will receive the data.
        /// @param data         RGBA pixel data; must remain valid until SubmitAsyncUploads runs.
        /// @return The same handle on success; invalid handle if no free upload slot.
        Rendering::Textures::TextureHandle  UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data, size_t data_size = 0);

        // Upload the ZUI font atlas synchronously using m_upload_cmd_mgr/m_sync_upload_fence.
        // Blocks until the GPU copy is complete so the texture is ready before the first
        // frame renders. Caller must enqueue the returned handle to
        // TextureHandleToUpdates for bindless descriptor registration.
        Rendering::Textures::TextureHandle  UploadFontAtlas(unsigned char* pixels, uint32_t width, uint32_t height);

        /// @brief Decode a texture file on the thread pool and upload it, async.
        /// @details When existing is valid, reconstructs that handle in place instead of
        ///          allocating a new one (used by IngestTexture's reimport path). The actual
        ///          upload happens later in CompleteDeferrals, against whichever frame is
        ///          current at that time — decode latency on the thread pool means a frame
        ///          index captured here would be stale by the time the upload runs.
        /// @param filename     Absolute path to the image file on disk.
        /// @param existing     Handle to reconstruct in place; invalid to allocate a new one.
        /// @param track_decode When true, retain the decode result for the render-thread caller.
        /// @return A valid TextureHandle that will become readable once the upload drains.
        Rendering::Textures::TextureHandle  SubmitTextureFile(const char* filename, Rendering::Textures::TextureHandle existing = {}, bool track_decode = false);

        /// @brief Decode a raster source into a bounded square texture for editor previews.
        /// @details The source is aspect-fit into a transparent square on a worker and then
        ///          follows the normal asynchronous upload and bindless-registration path.
        static constexpr uint32_t           ThumbnailTextureSlotReserve = 16;
        struct ThumbnailCacheIdentity
        {
            uuids::uuid AssetUUID        = {};
            uint64_t    SourceHash       = 0;
            uint64_t    DependencyHash   = 0;
            uint32_t    SchemaRevision   = 3;
            uint32_t    RendererRevision = 0;
        };
        Rendering::Textures::TextureHandle SubmitThumbnailTextureFile(const char* filename, uint32_t extent = 128, bool track_decode = true, const char* cache_filename = nullptr, const ThumbnailCacheIdentity* identity = nullptr, bool cache_only = false);

        /// Render-thread readback tickets own copied identity/path, never editor or preview task memory.
        /// At most four 128px readbacks/encodes can be outstanding. Failure only disables persistence.
        void*                              ReserveThumbnailCacheWrite(const char* filename, const ThumbnailCacheIdentity& identity);
        void                               AcceptThumbnailCacheWrite(void* context);
        static void                        CompleteThumbnailCacheReadback(const void* data, size_t size, void* context);
        static void                        CancelThumbnailCacheReadback(void* context);

        /// Schedule at most one worker-only sweep after the project scan settles.
        /// No enumeration, deletion, or inline worker fallback on the caller thread.
        bool                               SubmitThumbnailCacheCleanup(const char* project_root, uint32_t schema_revision, uint32_t renderer_revision);
        struct ThumbnailMaintenanceStatistics
        {
            uint64_t Runs           = 0;
            uint64_t Examined       = 0;
            uint64_t Removed        = 0;
            uint64_t ReclaimedBytes = 0;
            uint64_t Errors         = 0;
            bool     Pending        = false;
        };
        ThumbnailMaintenanceStatistics GetThumbnailMaintenanceStatistics();

        struct CpuUploadMemoryStatistics
        {
            size_t   BackingBytes            = 0;
            size_t   LiveBytes               = 0;
            size_t   LiveBlocks              = 0;
            // Sum of per-pool observed peaks, not a simultaneous process peak.
            size_t   SumOfPoolHighWaterBytes = 0;
            uint32_t ActiveDecodeLeases      = 0;
            uint32_t PendingWorkerTasks      = 0;
        };
        /// Individual synchronized pool samples; call before owner teardown.
        [[nodiscard]] CpuUploadMemoryStatistics GetCpuUploadMemoryStatistics() const;

        /// @brief Non-blocking release for an RRM-owned texture without an asset UUID.
        /// @details The render thread waits for tracked decode/submission and any producer
        ///          upload timeline before scheduling normal render-timeline retirement.
        void                                    ReleaseTexture(const Rendering::Textures::TextureHandle& handle);

        /// @brief Return the (255, 20, 147) fallback TextureHandle for missing textures, creating it on first call.
        Rendering::Textures::TextureHandle      GetOrCreateFallbackTexture();
        /// @brief Return the synchronous, neutral cubemap used while a scene environment is unavailable.
        Rendering::Textures::TextureHandle      GetOrCreateFallbackCubemap();
        /// @brief Returns the engine-global fallback IBL textures and versioned BRDF integration LUT.
        EnvironmentLightingResources            GetOrCreateFallbackEnvironmentLighting();

        /// @brief Returns the latest outcome for a tracked texture.
        /// @details Success is visible only after its upload has been submitted. Consumers
        ///          must still declare their graph read to acquire the streaming ticket.
        TextureDecodeState                      GetTextureDecodeState(const Rendering::Textures::TextureHandle& handle);
        /// @brief Stop observing one asynchronous decode after it has been consumed or discarded.
        void                                    ForgetTextureDecode(const Rendering::Textures::TextureHandle& handle);

        /// @brief Payload for a deferred texture upload.
        ///
        /// Flat struct — no heap ownership. Pixels points into a TLSFSlab allocation;
        /// CompleteDeferrals frees Pixels after staging has copied them, or on cancellation.
        /// When Slab is nullptr the pixels are borrowed (valid until CompleteDeferrals).
        struct TextureDeferral
        {
            uint8_t*                           Pixels          = nullptr;   ///< Pixel data (slab-owned or borrowed).
            size_t                             ByteSize        = 0;         ///< Size of Pixels in bytes.
            Core::Memory::TLSFSlab*            Slab            = nullptr;   ///< Owning slab; nullptr = borrowed pointer.
            uint8_t                            DecodeSlabIndex = UINT8_MAX; ///< Lease retained until this deferral is consumed or discarded.
            Rendering::Textures::TextureHandle TexHandle       = {};
        };

        /// @brief Enqueue a texture upload deferral for processing in the next BeginFrame.
        /// @param deferral Deferral to enqueue; if Slab is non-null, ownership of Pixels is transferred on success.
        /// @return False when the bounded deferral queue is full.
        bool                                                             EnqueueTextureDeferral(const TextureDeferral& deferral);

        /// @brief Drain all pending texture deferrals by dispatching UploadTextureBuffer.
        /// @details Called from AppRenderPipeline::BeginFrame, against the frame that's
        ///          current at the time each deferral actually drains (not when it was
        ///          enqueued — decode latency on the thread pool means those can be frames
        ///          apart). Deferrals that find no free upload slot are retried next frame.
        /// @param frame_index Render frame index to upload against.
        void                                                             CompleteDeferrals(uint8_t frame_index);

        /// @brief Submit all pending asynchronous texture uploads to their resolved GPU queues.
        /// @details Called from AppRenderPipeline::EndFrame. Processes m_async_uploads.
        void                                                             SubmitAsyncUploads();

        /// @brief Returns texture uploads that still require graph-side acquisition.
        /// @details Render-thread only. Tickets are published only after their producer
        ///          submission succeeds, so a graph never waits on work that was dropped.
        const Core::Containers::Array<Hardwares::StreamingUploadTicket>& GetStreamingUploadTickets() const;

        /// @brief Finds the outstanding streaming ticket for a texture, if any.
        const Hardwares::StreamingUploadTicket*                          FindStreamingUploadTicket(const Rendering::Textures::TextureHandle& handle) const;

        /// @brief Marks a graph-acquired texture upload ticket as consumed.
        /// @details Render-thread only. The ticket is removed after its acquire barrier
        ///          has been recorded into the consuming graph submission.
        void                                                             AcknowledgeStreamingUploadTicket(const Hardwares::StreamingUploadTicket& ticket);

        /// @brief Retire command buffers whose timeline fence has been signalled.
        /// @details Frees staging buffers associated with completed texture uploads for the
        ///          given frame/thread pool. Called from AppRenderPipeline::BeginFrame.
        /// @param frame_index  Render frame index.
        /// @param thread_index Thread index within the per-frame pool.
        void                                                             RetireTextureSlots(uint8_t frame_index, uint8_t thread_index);

        /// @brief Cancel queued texture upload jobs without submitting them.
        /// @details Called on swapchain recreation. Already-submitted geometry batches
        ///          remain published through their timeline semaphore.
        void                                                             ClearAsyncUploads();

        /// @brief Reset all texture timeline semaphore counters after a swapchain recreate.
        /// @details Re-initialises per-pool signal values and retire arrays to zero.
        void                                                             ResetTextureTimelines();

        /// @brief Schedule a hot-reload swap for a mesh buffer.
        /// @details Thread-safe: enqueues onto m_pending_swaps, applied by the next
        ///          FlushPendingSwaps (render thread). The current handle resolves to
        ///          the new geometry; pinned older versions keep their pool regions.
        ///          Upload submission retains the existing graphics/copy dependencies.
        /// @param old_handle The live BufferHandle to replace.
        /// @param new_asset  AssetHandle for the new version of the mesh.
        void                                                             ScheduleSwap(BufferHandle old_handle, Managers::AssetHandle new_asset);

        /// @brief Deferred release of a GPU buffer.
        /// @details Render-thread only. Generic allocations enter the timeline-gated
        ///          deferred-free queue. Mesh registrations are invalidated and their
        ///          regions reclaimed unless retained by an older geometry pin.
        /// @param handle Handle returned by the mesh upload path or UploadBuffer.
        void                                                             Release(BufferHandle handle);

        /// @brief Look up a generic device-local buffer by handle.
        /// @details Valid only for handles returned by UploadBuffer. Mesh handles must use
        ///          GetMeshOffsets instead. The returned pointer is valid for the current frame
        ///          only — do not store it across BeginFrame calls.
        /// @param handle Handle returned by UploadBuffer.
        /// @return Pointer to the BufferView, or nullptr if the handle is invalid or stale.
        const Core::Memory::BufferView*                                  GetBuffer(BufferHandle handle) const;

        /// @brief Return the shared device-local VkBuffer that holds all uploaded vertex data.
        /// @details All mesh uploads are appended sequentially at the watermark cursor.
        ///          Bound once to the VertexSB descriptor; per-draw vertex offset is in DrawData.
        /// @return Pointer to the global vertex BufferView; always valid after Initialize.
        const Core::Memory::BufferView*                                  GetGlobalVertexBuffer() const
        {
            return &m_pool.VertexBuffer;
        }

        /// @brief Return the shared device-local VkBuffer that holds all uploaded index data.
        /// @details Analogous to GetGlobalVertexBuffer for index (uint32) data.
        /// @return Pointer to the global index BufferView; always valid after Initialize.
        const Core::Memory::BufferView* GetGlobalIndexBuffer() const
        {
            return &m_pool.IndexBuffer;
        }

        /// @brief Return the pinned VkBuffer that holds builtin vertex data (grid and future static primitives).
        /// @details Written once at Setup time via RegisterBuiltinGeometry; never touched by
        ///          ResetGeometryBuffers or the streaming eviction path.
        /// @return Pointer to the builtin vertex BufferView; always valid after Initialize.
        const Core::Memory::BufferView* GetBuiltinVertexBuffer() const
        {
            return &m_builtin_vertex_buf;
        }

        /// @brief Return the pinned VkBuffer that holds builtin index data (grid and future static primitives).
        /// @details Analogous to GetBuiltinVertexBuffer for index (uint32) data.
        /// @return Pointer to the builtin index BufferView; always valid after Initialize.
        const Core::Memory::BufferView* GetBuiltinIndexBuffer() const
        {
            return &m_builtin_index_buf;
        }

        /// @brief Return true once at least one mesh has been appended to the global buffers.
        /// @details Used by GraphicRenderer to defer global-buffer descriptor binding until
        ///          data is present.
        bool GlobalBuffersReady() const
        {
            return m_pool.VtxCursor > 0;
        }

        /// @brief Query the element-count offsets for a mesh in the global geometry buffers.
        /// @param handle     BufferHandle returned by the mesh upload path.
        /// @param vtx_offset Out: index of the first DrawVertex element in the global VB.
        /// @param idx_offset Out: index of the first uint32 element in the global IB.
        /// @return true if the handle is valid and the offsets were written; false otherwise.
        bool                      GetMeshOffsets(BufferHandle handle, uint32_t& vtx_offset, uint32_t& idx_offset) const;

        /// Pin the current resident geometry version, without heap allocation.
        /// Returns empty for stale/non-mesh/non-resident handles or pin-table exhaustion.
        /// Render-thread only; a hot reload may coexist with the returned older version.
        MeshGeometryPin           PinMeshGeometry(BufferHandle handle);

        /// Render-thread draw admission: retains one implicit pin per version,
        /// shared by all draws/frames until every accepted submission completes.
        /// Does not imply first-frame dependency readiness or pin CPU asset metadata.
        bool                      GetFrameMeshGeometry(BufferHandle handle, MeshGeometrySnapshot* snapshot);

        static constexpr uint32_t ResourceFrameTimelineIndex = static_cast<uint32_t>(QueueType::COUNT);
        static constexpr uint32_t ResourceTimelineCount      = ResourceFrameTimelineIndex + 1;
        /// Submission owners call this only after Vulkan accepts work. Graph queue
        /// indices use QueueType; the final frame uses ResourceFrameTimelineIndex.
        void                      NotifyResourceSubmission(uint32_t timeline_index, Primitives::Semaphore* timeline, uint64_t value);
        /// Close recording on either final-submit success or abort. Accepted graph
        /// submissions remain gated even when the final presentation submit failed.
        void                      FinishResourceFrame();
        void                      PollFrameResources();
        /// Before a bound semaphore is destroyed, prove its retained uses complete.
        /// Nonblocking; false means the semaphore owner must not destroy it yet.
        bool                      ForgetResourceTimeline(Primitives::Semaphore* timeline);

        /// @brief Return true if the mesh's GPU region is fully uploaded and safe to draw.
        /// @details RenderScene calls this before emitting draw commands — meshes in Pending
        ///          or Unloaded state are skipped without stalling the render thread.
        bool                      IsMeshResident(BufferHandle handle) const;

        /// @brief Set the clock-hand Referenced bit for a resident mesh.
        /// @details Called by RenderScene for every mesh that emits a draw command this frame.
        ///          The eviction sweep clears the bit; meshes referenced within the last cycle
        ///          get one grace period before becoming eviction candidates.
        void                      MarkMeshReferenced(BufferHandle handle);

        /// @brief Enqueue a reload request for an evicted mesh slot.
        /// @details Called by RenderScene when a valid but non-resident handle is encountered.
        ///          Coalesces pending requests by slot generation; reloads are drained
        ///          in bounded slices during BeginFrame. Queue saturation is retryable.
        bool                      RequestMeshLoad(BufferHandle handle, const uuids::uuid& uuid);

        /// @brief Upload builtin geometry (grid and future static primitives) into the global vertex/index
        ///        buffers and return the element-count offsets.  Vertices must already be
        ///        laid out as DrawVertex (8 floats: xyz nxnynz uv). Indices must be uint32_t.
        /// @param vtx_data   Pointer to tightly-packed DrawVertex float data.
        /// @param vtx_bytes  Total byte size of the vertex data.
        /// @param idx_data   Pointer to uint32_t index array.
        /// @param idx_count  Number of indices.
        /// @param out_vtx_offset  Out: first DrawVertex element index in the global VB.
        /// @param out_idx_offset  Out: first uint32 element index in the global IB.
        void                      RegisterBuiltinGeometry(const void* vtx_data, size_t vtx_bytes, const uint32_t* idx_data, uint32_t idx_count, uint32_t& out_vtx_offset, uint32_t& out_idx_offset);

        /// @brief Request a geometry compaction on the next BeginFrame.
        ///        Resets the global vertex/index buffer cursors and slot map so the
        ///        next batch of mesh uploads starts at byte offset 0, reclaiming all
        ///        space occupied by the previous scene's geometry.
        ///        Thread-safe — may be called from the import/asset thread.
        void                      ResetGeometryBuffers();

        /// @brief Find the BufferHandle registered for a mesh asset by UUID.
        BufferHandle              FindMeshBuffer(const uuids::uuid& uuid) const;

        /// @brief Queue release of the currently registered mesh identity.
        /// @details Thread-safe. BeginFrame applies releases on the render thread,
        ///          which exclusively owns mesh-slot and geometry-pool mutation.
        ///          Each bounded UUID-map entry contains its own release-request bit,
        ///          so duplicates coalesce without a separate queue or overflow path.
        ///          Replacing the registration clears its old request; generation
        ///          checks protect reused slots. No-op if the UUID is absent.
        /// @param uuid Asset UUID of the mesh to release.
        void                      ReleaseMeshGeometry(const uuids::uuid& uuid);

        /// @brief Write CPU data into an existing HOST_VISIBLE BufferView.
        /// @details Uses the ring allocator for staging; falls back to a one-shot staging
        ///          buffer and RecordAndSubmit. Render-thread only.
        /// @param dst        Destination HOST_VISIBLE buffer (e.g. TransformSB, DrawDataSB).
        /// @param data       Source CPU data.
        /// @param byte_size  Number of bytes to write.
        /// @param dst_offset Byte offset within dst at which to begin writing.
        void                      UpdateBuffer(Hardwares::BufferView& dst, const void* data, size_t byte_size, uint32_t dst_offset = 0);

        /// @brief Upload arbitrary CPU data to a new device-local VkBuffer.
        /// @details Allocates a VkBuffer with the requested usage flags plus
        ///          VK_BUFFER_USAGE_TRANSFER_DST_BIT, stages data through the ring allocator
        ///          (or a fallback staging buffer), and submits via RecordAndSubmit.
        ///          Intended for per-entity or per-system GPU data that changes infrequently:
        ///          bone matrices, morph-target deltas, particle emitter configs, etc.
        ///          The handle must be passed to Release() when the buffer is no longer needed.
        /// @param data        CPU-side source data; must remain valid until the call returns.
        /// @param byte_size   Size in bytes of the data to upload.
        /// @param usage       VkBufferUsageFlags for the destination buffer (e.g. VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).
        /// @param debug_name  Optional label attached to the VmaAllocation for GPU debuggers.
        /// @return A valid BufferHandle on success; an invalid handle if allocation fails.
        BufferHandle              UploadBuffer(const void* data, size_t byte_size, VkBufferUsageFlags usage, const char* debug_name = nullptr);

        /// @brief Enqueue a VkShaderModule for timeline-gated deferred destruction.
        /// @details Used by shader hot-reload: call after all in-flight frames that reference
        ///          the old module have retired. The module is destroyed once the render
        ///          timeline semaphore advances past the current value.
        /// @param module The VkShaderModule to destroy; no-op if VK_NULL_HANDLE.
        void                      EnqueueDeletion(VkShaderModule module);

        /// @brief Enqueue a VkPipeline for timeline-gated deferred destruction.
        /// @details Used by pipeline hot-reload and render graph rebuilds.
        ///          Safe to call immediately after switching to the new pipeline.
        /// @param pipeline The VkPipeline to destroy; no-op if VK_NULL_HANDLE.
        void                      EnqueueDeletion(VkPipeline pipeline);

        /// @brief Enqueue a pre-built DeferredFreeEntry for timeline-gated deferred destruction.
        /// @details Low-level overload for callers that construct the entry themselves.
        ///          Prefer the typed overloads above when possible.
        void                      EnqueueDeletion(Hardwares::DeferredFreeEntry entry);

    private:
        // Textures never reach this queue — AssetManager::IngestTexture uploads them
        // directly, and reimports are triggered via ScheduleTextureReload instead.
        struct PendingUpload
        {
            Managers::AssetHandle Asset;
            uuids::uuid           UUID;
        };

        // A hot-reload swap request for a mesh buffer, queued by ScheduleSwap (asset thread)
        // and applied by FlushPendingSwaps (render thread, called from BeginFrame).
        // Immutable pinned versions survive repointing the public mesh handle.
        struct PendingSwap
        {
            BufferHandle          OldBuffer = {};
            Managers::AssetHandle NewAsset  = 0;
        };

        template <typename Resource>
        struct Slot
        {
            Resource Data       = {};
            uint32_t Generation = 0; // 0 = free
        };

        // Per-mesh record: byte-level region in the streaming pool (for Free on eviction/release),
        // element counts for draw-call assembly, and streaming lifecycle state.
        // Element offsets are derived on demand from Region.VtxByteOffset / DRAW_VERTEX_BYTES
        // and Region.IdxByteOffset / sizeof(uint32_t).
        struct MeshSlot
        {
            GeometryRegion          Region       = {};
            uint32_t                VtxCount     = 0;
            uint32_t                IdxCount     = 0;
            StreamingState          State        = StreamingState::Unloaded;
            bool                    Referenced   = false; ///< Clock-hand bit: set by RenderScene, cleared by eviction sweep.
            bool                    Pinned       = false; ///< Never evicted (builtins, or explicitly pinned).
            bool                    LoadFailed   = false; ///< Last queued residency attempt failed; a new explicit request can retry.
            uint64_t                Version      = 0;     ///< Content identity; changes even when the public handle does not.
            uint32_t                PinRecord    = UINT32_MAX;
            const MeshDrawMetadata* DrawMetadata = nullptr;
        };

        struct PinnedMeshGeometry
        {
            MeshGeometrySnapshot Snapshot                            = {};
            BufferHandle         Owner                               = {};
            uint32_t             PinCount                            = 0;
            bool                 Detached                            = false;
            bool                 FrameHeld                           = false;
            bool                 FramePending                        = false;
            uint64_t             RetireValues[ResourceTimelineCount] = {};
        };

        enum class SceneResourcePhase : uint8_t
        {
            Free,
            Writing,
            Collecting,
            Queued,
            Preparing,
            Ready,
            Failed,
            Releasing
        };
        enum class SceneInitialBufferStage : uint8_t
        {
            BuildCullingOrder,
            HeapifyTransparentOrder,
            SortTransparentOrder,
            AllocateTransforms,
            WriteTransforms,
            AllocateLights,
            WriteLights,
            AllocateMaterials,
            WriteMaterials,
            AllocateDraws,
            WriteDraws,
            AllocateCullingInput,
            WriteCullingInput,
            AllocateCulledIndirect,
            AllocateSelectionTokens,
            WriteSelectionTokens,
            AllocateCamera,
            WriteCamera,
            Complete
        };
        struct SceneResourcePreparation
        {
            std::atomic<SceneResourcePhase>               Phase{SceneResourcePhase::Free};
            std::atomic<uint32_t>                         Readers{0};
            std::atomic<bool>                             FrameRecorded{false};
            SceneResourcePreparationId                    Id                                = {};
            uint64_t                                      ExpectedGeneration                = 0;
            uint32_t                                      MeshCount                         = 0;
            uuids::uuid                                   Meshes[SceneGeometryMeshCapacity] = {};
            MeshGeometryPin                               Pins[SceneGeometryMeshCapacity]   = {};
            ScenePreparationContent                       Content                           = ScenePreparationContent::GeometryOnly;
            SceneEnvironmentInput                         EnvironmentInput                  = {};
            std::atomic<SceneEnvironmentPreparationState> Environment{SceneEnvironmentPreparationState::NotRequested};
            std::atomic<SceneResourcePreparationError>    EnvironmentError{SceneResourcePreparationError::None};
            // Render-owner-only retention/index/progress; never exposed while
            // Preparing. Identity/input fields above are fixed at Queued.
            uuids::uuid                                   Materials[SceneMaterialCapacity]            = {};
            MaterialVersionPin                            MaterialPins[SceneMaterialCapacity]         = {};
            uint32_t                                      FrameMaterialIndices[SceneMaterialCapacity] = {};
            // Fixed open-addressed UUID index, storing entry + 1; zero is empty.
            uint32_t                                      MaterialBuckets[SceneMaterialCapacity * 2]  = {};
            // Error/count are published by the Ready/Failed release transition.
            uint32_t                                      Cursor                                      = 0;
            BufferHandle                                  ResidencyWait                               = {};
            uint32_t                                      MaterialCount                               = 0;
            uint32_t                                      MaterialMeshCursor                          = 0;
            size_t                                        SubMeshCursor                               = 0;
            uint64_t                                      UploadValue                                 = 0;
            SceneResourcePreparationError                 Error                                       = SceneResourcePreparationError::None;
            MaterialVersionError                          MaterialError                               = MaterialVersionError::None;
            struct InitialInstance
            {
                ECS::EntityID Entity    = ECS::INVALID_ENTITY;
                uint32_t      MeshIndex = 0;
            };
            bool                            HasInitialFrame                                                 = false;
            uint32_t                        InitialInstanceCount                                            = 0;
            uint32_t                        InitialCopyCount                                                = 0;
            InitialInstance                 InitialInstances[SceneDrawCapacity]                             = {};
            Core::Maths::Mat4f              InitialTransforms[SceneDrawCapacity]                            = {};
            Scenes::LightArrayUBO           InitialLights                                                   = {};
            Scenes::SceneGridRenderSnapshot InitialGrid                                                     = {};
            Scenes::SceneInitialView        InitialView                                                     = {};
            Scenes::PreparedSceneDraw       InitialDraws[SceneDrawCapacity]                                 = {};
            uint32_t                        InitialDrawCount                                                = 0;
            uint32_t                        InitialInstanceCursor                                           = 0;
            size_t                          InitialSubMeshCursor                                            = 0;
            // Render-owner-only initial storage. Transform/light/material/draw
            // data is immutable; camera-dependent culling is frame-buffered.
            // Every allocation retires behind accepted consumers and readers.
            Core::Memory::BufferView        InitialTransformBuffer                                          = {};
            Core::Memory::BufferView        InitialCameraBuffer                                             = {};
            Core::Memory::BufferView        InitialLightBuffer                                              = {};
            Core::Memory::BufferView        InitialMaterialBuffer                                           = {};
            Core::Memory::BufferView        InitialDrawBuffer                                               = {};
            Core::Memory::BufferView        InitialCullingInputs[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]   = {};
            Core::Memory::BufferView        InitialCulledIndirect[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]  = {};
            Core::Memory::BufferView        InitialSelectionTokens[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT] = {};
            uint32_t                        InitialSelectionUploadCursor                                    = 0;
            uint32_t                        InitialCullingOrder[SceneDrawCapacity]                          = {};
            float                           InitialViewDepths[SceneDrawCapacity]                            = {};
            uint32_t                        InitialOrderCursor                                              = 0;
            uint32_t                        InitialOrderOpaqueCursor                                        = 0;
            uint32_t                        InitialOrderTransparentCursor                                   = 0;
            // Global material-table index -> this candidate's retained entry + 1.
            uint32_t                        InitialMaterialEntries[FrameMaterialCapacity]                   = {};
            uint32_t                        InitialMaterialTableSize                                        = 1;
            uint32_t                        InitialMaterialUploadCursor                                     = 0;
            uint32_t                        InitialDrawUploadCursor                                         = 0;
            uint32_t                        InitialCullingUploadCursor                                      = 0;
            uint32_t                        InitialCullingOpaqueCursor                                      = 0;
            uint32_t                        InitialCullingTransparentCursor                                 = 0;
            uint32_t                        InitialOpaqueDrawCount                                          = 0;
            uint32_t                        InitialBufferFrame                                              = 0;
            uint32_t                        InitialTransformUploadCursor                                    = 0;
            SceneInitialBufferStage         InitialBufferStage                                              = SceneInitialBufferStage::AllocateTransforms;
            bool                            InitialBuffersPending                                           = false;
            uint64_t                        InitialBufferRetireValues[ResourceTimelineCount]                = {};
        };

        // UUID→handle entry for hot-reload swap lookup. Protected by m_uuid_map_mutex.
        struct UUIDBufferPair
        {
            uuids::uuid  UUID             = {};
            BufferHandle Handle           = {};
            bool         ReleaseRequested = false;
        };

        static constexpr uint32_t DRAW_VERTEX_BYTES     = 8 * sizeof(float); // x y z nx ny nz u v
        static constexpr uint32_t MAX_BUFFERS           = SceneGeometryMeshCapacity;
        static constexpr uint32_t MAX_GENERIC_BUFS      = 4096;
        // Generation tag: bit 31 = 1 marks a generic-buffer handle so Release() and
        // GetBuffer() can distinguish them from mesh handles (bit 31 = 0).
        static constexpr uint32_t GBUF_GEN_TAG          = 0x8000'0000u;
        static constexpr uint32_t MAX_UUID_MAP          = 4096;
        static constexpr uint32_t MAX_PENDING           = 1024;
        static constexpr uint32_t MAX_TEXTURE_DEFERRALS = 8192;
        static constexpr uint32_t MAX_TEXTURE_TASKS     = 4096;

        // Per-frame-index batch state: the m_batch_timeline value last signalled for that
        // frame index's batch, and the staging buffers still owned by it. Retired
        // proactively every frame by RetireBatchStagings once m_batch_timeline reaches
        // LastSignal, with BeginBatchUpload's own reuse-wait as a guaranteed fallback.
        // Self-contained — no DeferFree/RenderTimeline dependency.
        struct BatchFrameState
        {
            uint64_t                 LastSignal                      = 0;
            Core::Memory::BufferView StagingBuffers[MAX_PENDING * 2] = {};
            uint32_t                 StagingCount                    = 0;
        };
        // Upper bound for texture timeline slot search — keeps textures out of geometry
        // slots and caps the retire loop to the same range.
        static constexpr uint32_t     GEOMETRY_UPLOAD_SLOT              = 15;
        // A decode may need an equirectangular-cubemap-sized output (about 96 MiB).
        // The fixed lease count is deliberately independent of thread-pool worker count:
        // a lease remains occupied until its decoded pixels reach the GPU or are discarded.
        static constexpr uint32_t     MAX_CONCURRENT_TEXTURE_DECODES    = 4;
        static constexpr size_t       UPLOAD_SLAB_BYTES                 = 128 * 1024 * 1024;
        static constexpr size_t       TEXTURE_TASK_SLAB_BYTES           = ZMega(2);
        static constexpr size_t       SYNCHRONOUS_TEXTURE_SCRATCH_BYTES = ZMega(4);
        // Written once at Setup time via RegisterBuiltinGeometry; never reset by
        // ResetGeometryBuffers and never touched by the streaming eviction path.
        static constexpr VkDeviceSize BUILTIN_VTX_CAPACITY              = 1 * 1024 * 1024; // 1 MB — ample for all engine builtins
        static constexpr VkDeviceSize BUILTIN_IDX_CAPACITY              = 1 * 1024 * 1024;

        BufferHandle                  DoUploadMesh(Managers::AssetHandle asset, uint32_t frame_index);

        struct TextureDecodeTask
        {
            RenderResourceManager*               Owner                              = nullptr;
            Specifications::TextureSpecification Specification                      = {};
            Rendering::Textures::TextureHandle   Texture                            = {};
            uint8_t                              DecodeSlabIndex                    = UINT8_MAX;
            bool                                 IsEnvironmentMap                   = false;
            bool                                 TrackCompletion                    = false;
            bool                                 FitIntoSquare                      = false;
            bool                                 CacheOnly                          = false;
            ThumbnailCacheIdentity               CacheIdentity                      = {};
            char                                 Filename[MAX_FILE_PATH_COUNT]      = {};
            char                                 CacheFilename[MAX_FILE_PATH_COUNT] = {};
        };

        struct ThumbnailCacheWriteTask
        {
            RenderResourceManager* Owner                         = nullptr;
            ThumbnailCacheIdentity Identity                      = {};
            char                   Filename[MAX_FILE_PATH_COUNT] = {};
            uint8_t                Pixels[128 * 128 * 4]         = {};
            bool                   Reserved                      = false;
            bool                   Accepted                      = false;
            bool                   WorkerQueued                  = false;
        };

        struct ThumbnailCacheCleanupTask
        {
            RenderResourceManager* Owner                            = nullptr;
            char                   ProjectRoot[MAX_FILE_PATH_COUNT] = {};
            uint32_t               SchemaRevision                   = 0;
            uint32_t               RendererRevision                 = 0;
            bool                   WorkerQueued                     = false;
        };

        struct TrackedTextureDecode
        {
            Rendering::Textures::TextureHandle Texture = {};
            TextureDecodeState                 State   = TextureDecodeState::Untracked;
        };

        struct TextureDecodeTracker
        {
            // Direct indexed by bindless slot, checked against its generation.
            // Replaces the similarly sized completion ring: no extra arena and
            // no queue-overflow path can lose an initial texture's terminal state.
            static constexpr uint32_t MaxTracked          = TextureVersionCapacity;
            TrackedTextureDecode      Entries[MaxTracked] = {};
        };

        using TextureDecodeTaskQueue = Core::Containers::MPSCQueue<TextureDecodeTask*, MAX_TEXTURE_TASKS>;

        /// @brief Append one mesh asset's vertex/index data to the global buffers.
        /// @details Shared by DoUploadMesh (new slot) and FlushPendingSwaps (reuse slot).
        ///          Returns a zero-VtxCount MeshSlot on failure.
        MeshSlot                           AppendMeshData(Managers::AssetHandle asset, uint32_t frame_index);

        void                               AppendToGlobalBuffer(Core::Memory::BufferView& dst_buf, const void* data, size_t byte_size, VkDeviceSize byte_offset, uint32_t frame_index);
        void                               FlushPendingUploads(uint32_t frame_index);

        /// @brief Drain m_pending_swaps and apply each swap immediately (render thread only).
        void                               FlushPendingSwaps(uint32_t frame_index);
        Rendering::Textures::TextureHandle SubmitTextureFileInternal(const char* filename, Rendering::Textures::TextureHandle existing, bool track_decode, uint32_t thumbnail_extent, const char* cache_filename = nullptr, const ThumbnailCacheIdentity* identity = nullptr, bool cache_only = false);

        /// @brief Drain m_pending_texture_reloads and reimport each one (render thread only).
        void                               FlushPendingTextureReloads();
        void                               PollTextureReplacements();
        void                               CancelTextureReplacements();

        /// @brief Drain m_pending_texture_releases and call Device->DestroyTexture (render thread only).
        void                               FlushPendingTextureReleases();
        bool                               QueueTextureRelease(const Rendering::Textures::TextureHandle& handle);
        static void                        OnAssetReady(void* context, const uuids::uuid& uuid, Managers::AssetHandle handle);
        static void                        OnAssetStale(void* context, const uuids::uuid& uuid);
        static void                        OnAssetRemoved(void* context, const uuids::uuid& uuid, Managers::AssetType type);
        static void                        RunTextureDecodeTask(void* context);
        static bool                        AspectFitThumbnail(const uint8_t* source, int width, int height, uint8_t* destination, uint32_t extent);
        static bool                        WriteThumbnailCache(const char* filename, const uint8_t* pixels, uint32_t width, uint32_t height, const ThumbnailCacheIdentity& identity, bool bottom_up = true);
        static void                        RunThumbnailCacheWrite(void* context);
        static void                        RunThumbnailCacheCleanup(void* context);
        void                               CancelStoppedThumbnailCacheWrites();
        static uint8_t*                    ReadThumbnailCache(const char* filename, uint32_t width, uint32_t height, const ThumbnailCacheIdentity& identity);
        void                               CompleteTextureDecodeTask(TextureDecodeTask* task);
        bool                               TryAcquireTextureDecodeSlab(uint8_t* out_index);
        void                               ReleaseTextureDecodeSlab(uint8_t index);
        bool                               TrackTextureDecode(const Rendering::Textures::TextureHandle& handle);
        void                               PublishTextureDecodeCompletion(const Rendering::Textures::TextureHandle& handle, bool success);
        void                               MarkTextureUploadSubmitted(const Rendering::Textures::TextureHandle& handle);
        bool                               ProcessTextureDeferral(uint8_t frame_index, TextureDeferral& deferral);
        bool                               TextureReleaseRequested(Textures::TextureHandle handle);
        void                               ReleaseTextureDeferral(TextureDeferral& deferral);
        void                               DiscardTextureDeferrals();
        void                               PublishStreamingUploadTicket(const Hardwares::StreamingUploadTicket& ticket);
        bool                               BeginTextureUploadRecording(Textures::TextureHandle handle, bool graphics_upload);
        QueueType                          TextureUploadQueue(Textures::TextureHandle handle, Specifications::ImageLayout layout) const;
        bool                               TextureProducerComplete(Textures::TextureHandle handle, bool* failed = nullptr);
        static void                        OnStreamingUploadSubmitted(void* context, const Hardwares::StreamingUploadTicket& ticket);
        static void                        OnStreamingUploadRejected(void* context, const Hardwares::StreamingUploadTicket& ticket);
        void                               RejectTextureUpload(const Hardwares::StreamingUploadTicket& ticket);
        void                               ResetTextureUploadSlot(Rendering::QueueType queue, uint32_t pool, uint32_t slot);

        void                               BeginBatchUpload(uint8_t frame_index);
        void                               EndBatchUpload();

        /// @brief Reuse this slot's open batch, or submit the previous slot's batch
        ///        before opening a new one. Every upload joins through this method.
        void                               EnsureBatchOpen(uint8_t frame_index);

        /// @brief Free frame-index batch stagings once m_batch_timeline reaches LastSignal.
        ///        Called once per frame from BeginFrame.
        void                               RetireBatchStagings();

        /// @brief Start queued texture decodes while a bounded decode slab is available.
        /// @details Render-thread only. Queued requests preserve their texture handles;
        ///          they wait here rather than being replaced with the fallback texture.
        void                               DispatchQueuedTextureDecodes();
        void                               DiscardQueuedTextureDecodes();

        void                               ResetGeometryBuffersInternal();
        /// All geometry-reclamation paths must detach pinned versions rather than free them.
        void                               RetireMeshGeometry(MeshSlot& geometry);
        static const MeshDrawMetadata*     CaptureMeshDrawMetadata(Core::Memory::TLSFSlab& slab, const Importers::AssetMesh& mesh);
        static void                        ReleaseMeshDrawMetadata(const MeshDrawMetadata* metadata);
        void                               ReleaseTextureVersionPin(uint32_t slot, uint64_t version);
        TextureVersionPin                  PinTextureVersionInternal(Textures::TextureHandle handle, bool graph_owned);
        MaterialVersionPin                 RetainMaterialVersion(const Meshes::MeshMaterial& material);
        TextureVersionPin                  CloneTextureVersion(const TextureVersionPin& retained);
        MaterialVersionPin                 CloneMaterialVersion(const MaterialVersionPin& retained);
        bool                               SceneTextureProducersReady(const MaterialVersionPin& material, bool* failed);
        SceneTextureDescriptorState        PrepareMaterialDescriptorVersions(MaterialVersionPin& material);
        bool                               MaterialDescriptorVersionsCurrent(const MaterialVersionPin& material) const;
        bool                               GetFrameMaterialVersionInternal(const Meshes::MeshMaterial& material, const MaterialVersionPin* retained, uint32_t* index, MaterialVersionSnapshot* snapshot, bool recording = true);
        bool                               RetainFrameMeshGeometry(MeshGeometryPin&& pin, MeshGeometrySnapshot* snapshot);
        bool                               HasUnsubmittedTextureWork() const;
        void                               AdmitTextureVersionRetirements();
        void                               ReplaceMeshGeometry(MeshSlot& geometry, const MeshSlot& replacement);
        void                               ReleaseMeshGeometryPin(uint32_t record, uint64_t version);
        void                               TickSceneResourcePreparations(uint64_t completed, bool upload_failed);
        static uint32_t                    PreparedMaterialBucket(const SceneResourcePreparation& record, const uuids::uuid& material);
        const MeshGeometryPin*             FindPreparedSceneGeometry(SceneResourcePreparationId id, const uuids::uuid& mesh) const;
        const MaterialVersionPin*          FindPreparedSceneMaterial(SceneResourcePreparationId id, const uuids::uuid& material) const;
        void                               ReleaseSceneResourcePreparationsAfterIdle();
        void                               ReleaseSceneAndFrameResourcesAfterIdle();
        void                               PrepareSceneFrameBuffers(SceneResourcePreparation& record, uint32_t& budget);
        static void                        BeginSceneFrameBufferPreparation(SceneResourcePreparation& record);
        void                               ReleaseSceneFrameBuffers(SceneResourcePreparation& record, uint32_t* budget = nullptr);
        bool                               HasPreparedSceneBuffers(bool pending_only = false) const;
        static bool                        HasSceneFrameBuffers(const SceneResourcePreparation& record);
        SceneResourceLease                 AcquireSceneResourceReader(SceneResourcePreparationId id);
        void                               StampResourceSubmission(uint32_t timeline_index, uint64_t value);
        void                               CollectFrameResources(const uint64_t* completed);
        void                               ReleaseFrameResourcesAfterIdle();
        void                               ReleaseFrameMaterial(uint32_t index);
        void                               ReleaseFrameMaterialReservation(uint32_t index);
        uint64_t                           NextMeshGeometryVersion();
        /// @brief Drain captured mesh-release identities before uploads/swaps (render thread only).
        void                               FlushPendingMeshReleases();
        /// @brief Re-pack all Resident mesh regions from offset 0, eliminating fragmentation holes.
        /// @details Resets the pool and re-uploads every Resident mesh's CPU asset data into
        ///          fresh batch regions using the existing AppendMeshData path. Slot regions are
        ///          updated in place before RenderScene runs. EndFrame submits the batch, and
        ///          Present() waits on m_batch_timeline before drawing the new data.
        void                               RunCompaction();
        void                               InitUploadPool();
        void                               InitGlobalBuffers();
        void                               InitUploadSlabs();
        void                               InitTextureTimelines();
        void                               ShutdownTextureTimelines();
        Textures::TextureHandle            CreateSynchronousTexture(const Specifications::TextureSpecification& specification, const void* pixels, cstring debug_name);
        uint32_t                           AllocMeshSlot();
        uint32_t                           AllocGBufSlot();

        /// @brief Advance counter and return the next GBUF_GEN_TAG-tagged generation value.
        static uint32_t                    NextGBufGeneration(uint32_t& counter);

        friend class GeometryStreamingManager;
        friend struct ::RRMTestHelper; // test-only — grants slot state access to streaming manager tests
        friend struct ::RRMDecodeTestHelper;
        friend struct ::RRMUploadBatchTestHelper;

        Hardwares::VulkanDevice*                 m_device                                            = nullptr;
        ISceneTextureDescriptorPreparation*      m_scene_descriptor_preparation                      = nullptr;
        Core::VFS::AssetRegistry*                m_registry                                          = nullptr;

        // Dedicated command buffer manager for geometry/font-atlas uploads, separate from
        // Device->CommandBufferMgr. Regular pool slot 0 serves the two remaining
        // synchronous callers (UploadFontAtlas, UpdateBuffer's ring path); the instant
        // pool serves BeginBatchUpload/EndBatchUpload, submitted without a CPU wait.
        Hardwares::CommandBufferManagerPtr       m_upload_cmd_mgr                                    = {};
        // The only two remaining synchronous, fence-blocking uploads (UploadFontAtlas and
        // UpdateBuffer's ring path) are both render-thread-only and always fully block
        // before returning, so one shared fence is enough — neither can ever be in flight
        // when the other starts.
        Rendering::Primitives::Fence*            m_sync_upload_fence                                 = nullptr;
        Hardwares::AsyncUploadQueue              m_async_uploads                                     = {};

        // Streaming geometry pool — owns the global VB/IB and their free lists.
        GeometryPool                             m_pool                                              = {};
        GeometryStreamingManager                 m_streaming_mgr                                     = {};

        // Separate from the global streaming pool so a scene reload cannot corrupt the
        // builtin draw offsets via ResetGeometryBuffers.
        Core::Memory::BufferView                 m_builtin_vertex_buf                                = {};
        Core::Memory::BufferView                 m_builtin_index_buf                                 = {};
        VkDeviceSize                             m_builtin_vtx_cursor                                = 0;
        VkDeviceSize                             m_builtin_idx_cursor                                = 0;

        // Dedicated timeline semaphore — single writer (EndBatchUpload), intentionally NOT
        // DeviceSwapchain::RenderTimeline. Sharing RenderTimeline with Present()'s own
        // two-per-frame increments produced non-monotonic values on Intel's Windows driver.
        Rendering::Primitives::Semaphore*        m_batch_timeline                                    = nullptr;
        Hardwares::CommandBuffer*                m_batch_cmd                                         = nullptr;
        Core::Containers::Array<BatchFrameState> m_batch_frames                                      = {};
        uint64_t                                 m_batch_next_value                                  = 0;
        uint64_t                                 m_batch_accepted_value                              = 0;
        bool                                     m_batch_submission_failed                           = false;
        SceneResourcePreparation                 m_scene_resources[SceneResourcePreparationCapacity] = {};
        uint64_t                                 m_scene_resources_sequence                          = 0;
        uint32_t                                 m_scene_resources_turn                              = 0;
        std::atomic<bool>                        m_scene_resources_sealed{false};
        // Single-byte batch fields packed together to eliminate alignment padding gaps.
        uint8_t                                  m_batch_frame_index                            = 0;
        // Set at BeginFrame — lets upload paths not threaded through a frame_index param
        // (UpdateBuffer, UploadFontAtlas) pick the correct per-frame command buffer.
        uint8_t                                  m_active_frame_index                           = 0;
        bool                                     m_batch_mode                                   = false;

        // Bounded TLSF decode slabs carved from the ImportPipeline owner. Each occupied
        // slot owns one decoded result through upload completion or discard.
        Core::Memory::ArenaAllocator*            m_upload_arena                                 = nullptr;
        Core::Memory::TLSFSlab                   m_upload_slabs[MAX_CONCURRENT_TEXTURE_DECODES] = {};
        PaddedAtomic<uint32_t>                   m_active_texture_decode_slabs                  = {};
        Core::Memory::TLSFSlab                   m_synchronous_texture_scratch                  = {};
        Core::Memory::TLSFSlab                   m_texture_task_slab                            = {};
        PaddedAtomic<uint32_t>                   m_pending_texture_decodes                      = {};
        PaddedAtomic<bool>                       m_accept_texture_decodes                       = {};
        ThumbnailCacheWriteTask                  m_thumbnail_cache_writes[4]                    = {};
        ThumbnailCacheCleanupTask                m_thumbnail_cache_cleanup                      = {};
        ThumbnailMaintenanceStatistics           m_thumbnail_maintenance_stats                  = {};

        // SubmitTextureFile can be called by importer threads. The render thread drains
        // this queue whenever a decode slab becomes available, preserving backpressure
        // without dropping texture requests from large imports.
        TextureDecodeTaskQueue                   m_queued_texture_decodes                       = {};

        TextureDecodeTracker                     m_texture_decode_tracker                       = {};
        // Workers publish failure, and render submission publishes success.
        // A short lock serializes direct-indexed updates and UI/render observers.
        std::mutex                               m_texture_decode_mutex                         = {};
        Rendering::Textures::TextureHandle       m_fallback_cubemap                             = {};
        EnvironmentLightingResources             m_fallback_environment_lighting                = {};

        Slot<MeshSlot>                           m_mesh_slots[MAX_BUFFERS]                      = {};
        uint32_t                                 m_mesh_slot_count                              = 0;
        // Monotonic per-slot counter, never reset by Release() — provides ABA protection
        // on slot reuse (a stale handle with the old generation is never re-validated).
        uint32_t                                 m_mesh_slot_gen_counter[MAX_BUFFERS]           = {};

        // At most MAX_BUFFERS distinct pinned versions, including detached old content.
        // Pin saturation refuses new pins; it never evicts a version already promised.
        PinnedMeshGeometry                       m_pinned_mesh_geometry[MAX_BUFFERS]            = {};
        uint32_t                                 m_pinned_mesh_geometry_count                   = 0;
        uint32_t                                 m_next_mesh_pin_record                         = 0;
        uint64_t                                 m_mesh_geometry_version                        = 0;
        uint32_t                                 m_frame_geometry_count                         = 0;
        uint32_t                                 m_pending_frame_geometry_count                 = 0;
        Primitives::Semaphore*                   m_resource_timelines[ResourceTimelineCount]    = {};
        uint64_t                                 m_resource_completed[ResourceTimelineCount]    = {};

        // Handles carry GBUF_GEN_TAG in bit 31 to distinguish from mesh handles.
        Slot<Core::Memory::BufferView>           m_gbuf_slots[MAX_GENERIC_BUFS]                 = {};
        uint32_t                                 m_gbuf_slot_count                              = 0;
        uint32_t                                 m_gbuf_slot_gen_counter[MAX_GENERIC_BUFS]      = {};

        // Written on first upload; read on OnAssetStale.
        UUIDBufferPair                           m_uuid_to_buffer[MAX_UUID_MAP]                 = {};
        uint32_t                                 m_uuid_to_buffer_count                         = 0;
        std::mutex                               m_uuid_map_mutex;

        PendingUpload                            m_pending[MAX_PENDING] = {};
        uint32_t                                 m_pending_count        = 0;
        std::mutex                               m_pending_mutex;

        // Dedicated mutex — m_pending_mutex is held across file I/O and a GPU call inside
        // SubmitTextureFile; swap enqueue must not stall behind a concurrent texture load.
        PendingSwap                              m_pending_swaps[MAX_PENDING] = {};
        uint32_t                                 m_pending_swap_count         = 0;
        std::mutex                               m_pending_swap_mutex;

        // Set by asset thread, executed on render thread.
        std::atomic<bool>                        m_pending_reset                        = false;

        // Deduped by UUID at enqueue time.
        uuids::uuid                              m_pending_texture_reloads[MAX_PENDING] = {};
        uint32_t                                 m_pending_texture_reload_count         = 0;
        std::mutex                               m_pending_texture_reload_mutex;

        // Render-owner records. A replacement owns its decode/upload until either
        // checked publication or cancellation, then retains its retirement target
        // until the bounded release queue accepts it. No per-reload heap storage.
        static constexpr uint32_t                TextureReplacementCapacity = 16;
        struct TextureReplacement
        {
            uuids::uuid                      UUID             = {};
            Textures::TextureHandle          Expected         = {};
            Textures::TextureHandle          Replacement      = {};
            Textures::TextureHandle          Retiring         = {};
            Hardwares::StreamingUploadTicket Producer         = {};
            bool                             Superseded       = false;
            bool                             Resolved         = false;
            bool                             ProducerComplete = false;
        };
        TextureReplacement                 m_texture_replacements[TextureReplacementCapacity] = {};
        std::atomic<uint32_t>              m_texture_replacement_count                        = 0;

        // Handle captured up front in ReleaseTexture; FlushPendingTextureReleases is the
        // only caller of Device->DestroyTexture.
        Rendering::Textures::TextureHandle m_pending_texture_releases[MAX_PENDING]            = {};
        uint32_t                           m_pending_texture_release_count                    = 0;
        std::mutex                         m_pending_texture_release_mutex;

        struct PinnedTextureVersion
        {
            TextureVersionSnapshot Snapshot           = {};
            uint32_t               References         = 0;
            bool                   DestroyRequested   = false;
            bool                   PreviouslyAdmitted = false;
        };
        PinnedTextureVersion m_texture_versions[TextureVersionCapacity] = {};
        struct FrameTextureVersion
        {
            TextureVersionPin Pin                                 = {};
            uint64_t          RetireValues[ResourceTimelineCount] = {};
            bool              Pending                             = false;
        };
        // Direct slot lookup plus a dense active list: admission, stamping and
        // collection never scan the full texture-handle space for sparse scenes.
        FrameTextureVersion       m_frame_textures[TextureVersionCapacity]      = {};
        uint32_t                  m_frame_texture_slots[TextureVersionCapacity] = {};
        uint32_t                  m_frame_texture_count                         = 0;
        uint32_t                  m_pending_frame_texture_count                 = 0;
        mutable std::mutex        m_texture_version_mutex;
        uint64_t                  m_texture_version_sequence                              = 0;
        uint64_t                  m_material_version_sequence                             = 0;
        uint64_t                  m_texture_version_pin_count                             = 0;
        uint64_t                  m_texture_retirement_ready[TextureVersionCapacity / 64] = {};

        static constexpr uint32_t FrameMaterialBucketCount                                = FrameMaterialCapacity * 2;
        struct FrameMaterialVersion
        {
            MaterialVersionPin Pin                                 = {};
            uint64_t           Hash                                = 0;
            uint32_t           Next                                = 0; // bucket links are one-based; zero is empty
            uint32_t           Reservations                        = 0;
            bool               Pending                             = false;
            uint64_t           RetireValues[ResourceTimelineCount] = {};
        };
        FrameMaterialVersion                                                       m_frame_materials[FrameMaterialCapacity]           = {};
        uint32_t                                                                   m_frame_material_buckets[FrameMaterialBucketCount] = {};
        uint32_t                                                                   m_frame_material_count                             = 0;
        uint32_t                                                                   m_pending_frame_material_count                     = 0;
        uint32_t                                                                   m_frame_material_free_hint                         = 0;

        Core::Containers::Array<Rendering::Primitives::Semaphore*>                 m_tex_timelines                                    = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*>                 m_tex_transfer_timelines                           = {};
        Core::Containers::Array<Core::Containers::Array<uint64_t>>                 m_tex_retire_values                                = {};
        Core::Containers::Array<Core::Containers::Array<uint64_t>>                 m_tex_transfer_retire                              = {};
        Core::Containers::Array<Core::Containers::Array<Core::Memory::BufferView>> m_tex_retire_staging                               = {};
        Core::Containers::Array<Core::Containers::Array<Core::Memory::BufferView>> m_tex_transfer_staging                             = {};
        Core::Containers::Array<std::atomic_uint64_t>                              m_tex_next_values                                  = {};
        Core::Containers::Array<std::atomic_uint64_t>                              m_tex_transfer_next_values                         = {};
        Core::Containers::MPSCQueue<TextureDeferral, MAX_TEXTURE_DEFERRALS>        m_tex_deferral_queue                               = {};
        // Render-thread-owned deferrals that could not claim an upload slot this frame.
        // They stay here until a later frame completes the upload.
        Core::Containers::Array<TextureDeferral>                                   m_tex_deferral_retry                               = {};
        // Tickets are render-thread-owned and persist across frame-graph rebuilds until
        // their consumer acquire-bearing submission has been accepted.
        Core::Containers::Array<Hardwares::StreamingUploadTicket>                  m_streaming_upload_tickets                         = {};
        // Completion proof outlives acquire acknowledgement. The render owner
        // publishes/polls it; m_texture_version_mutex also protects destruction
        // requests made by other owners. A queued consumer may acknowledge its
        // ticket before the producer finishes.
        struct TextureProducerCompletion
        {
            Textures::TextureHandle Texture            = {};
            Primitives::Semaphore*  Timeline           = nullptr;
            uint64_t                Value              = 0;
            bool                    PendingSubmission  = false;
            bool                    AcquirePending     = false;
            bool                    GraphicsUploadOnly = false;
        };
        TextureProducerCompletion m_texture_producer_completions[TextureVersionCapacity] = {};
        uint32_t                  m_tex_total_cmd_count                                  = 0;
    };

} // namespace ZEngine::Rendering
