#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Hardwares/DeferredFreeQueue.h>
#include <ZEngine/Helpers/ThreadSafeQueue.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Pools/CommandPool.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <ZEngine/Rendering/RenderHandle.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <mutex>
#include <variant>
#include <vector>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
    struct CommandBuffer;
} // namespace ZEngine::Hardwares

namespace ZEngine::Rendering
{
    // RenderResourceManager — single authority over GPU buffer/image lifetime.
    //
    // Sits on EngineContext between the asset layer (AssetRegistry, AssetManager)
    // and the GPU layer (VulkanDevice, GpuAllocator). Neither layer knows about
    // the other; all coupling flows through here.
    //
    // THREAD SAFETY:
    //   Initialize / Shutdown     — main thread, once at startup/teardown
    //   BeginFrame / EndFrame     — render thread only
    //   OnAssetReady / OnAssetStale callbacks — asset/import thread; protected by m_pending_mutex
    //   GetBuffer / GetImage      — render thread read; asset thread writes via pending queue
    class RenderResourceManager
    {
    public:
        static constexpr uint32_t          FRAMES_IN_FLIGHT = 3;

        /// @brief Initialize the RRM and bind it to a VulkanDevice and AssetRegistry.
        /// @details Registers OnAssetReady and OnAssetStale callbacks on the registry,
        ///          allocates the dedicated upload command pool and fence, and creates
        ///          the packed global vertex and index buffers.
        /// @param device   The active Vulkan device; must outlive this RRM instance.
        /// @param registry The asset registry to subscribe to; must outlive this RRM instance.
        void                               Initialize(Hardwares::VulkanDevice* device, Core::VFS::AssetRegistry* registry);

        /// @brief Drain in-flight GPU work and release all GPU resources.
        /// @details Calls vkQueueWaitAll, shuts down texture timelines, frees the global
        ///          geometry buffers, all image slots, and all generic buffer slots.
        ///          Must be called from the main thread before VulkanDevice teardown.
        void                               Shutdown();

        /// @brief Per-frame render-thread entry point.
        /// @details Flushes pending mesh and texture uploads that were queued from the
        ///          asset thread since the previous BeginFrame. Called by AppRenderPipeline.
        /// @param frame_index Current swapchain frame index (0 .. FRAMES_IN_FLIGHT-1).
        void                               BeginFrame(uint32_t frame_index);

        /// @brief Per-frame render-thread exit point.
        /// @details Drains hot-reload swap entries that have aged past FRAMES_IN_FLIGHT
        ///          frames. Called by AppRenderPipeline after command buffer submission.
        /// @param frame_index Current swapchain frame index (0 .. FRAMES_IN_FLIGHT-1).
        void                               EndFrame(uint32_t frame_index);

        /// @brief Upload a mesh asset to the packed global vertex and index buffers.
        /// @details Thread-safe: enqueues a pending upload consumed by BeginFrame.
        ///          Vertices and indices are appended at the current watermark cursor.
        /// @param asset_handle Handle to a fully imported MeshAsset in the AssetManager.
        /// @return A valid BufferHandle on success; invalid if the asset is null or upload fails.
        BufferHandle                       UploadMesh(Managers::AssetHandle asset_handle);

        /// @brief Upload a texture asset to a new device-local VkImage.
        /// @details Thread-safe: enqueues a pending upload consumed by BeginFrame.
        ///          The image is transitioned to SHADER_READ_ONLY_OPTIMAL via timeline semaphore.
        /// @param asset_handle Handle to a fully imported TextureAsset in the AssetManager.
        /// @return A valid ImageHandle on success; invalid if the asset is null or upload fails.
        ImageHandle                        UploadTexture(Managers::AssetHandle asset_handle);

        /// @brief Upload raw RGBA pixel data to an existing TextureHandle via the timeline path.
        /// @details Records a staging copy into an instant command buffer and enqueues the
        ///          submission to the timeline job queue. Actual GPU submission drains in
        ///          SubmitTextureJobs(). For font atlas upload prefer UploadFontAtlas.
        /// @param frame_index  Render frame index used to select the per-frame command pool.
        /// @param thread_index Thread index within the pool.
        /// @param handle       Pre-allocated TextureHandle whose VkImage will receive the data.
        /// @param data         RGBA pixel data; must remain valid until SubmitTextureJobs runs.
        /// @return The same handle on success; invalid handle if no free upload slot.
        Rendering::Textures::TextureHandle UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data);

        // Upload the ImGui font atlas synchronously using m_upload_cmd/m_upload_fence.
        // Blocks until the GPU copy is complete so the texture is ready before the first
        // frame renders. Caller must enqueue the returned handle to
        // TextureHandleToUpdates for bindless descriptor registration.
        Rendering::Textures::TextureHandle UploadFontAtlas(unsigned char* pixels, uint32_t width, uint32_t height);

        /// @brief Load a texture file from disk, decode it on the thread pool, and upload.
        /// @details Asynchronously reads and decodes the image; uploads via the timeline path.
        /// @param frame_index  Render frame index.
        /// @param thread_index Thread index within the per-frame pool.
        /// @param filename     Absolute path to the image file on disk.
        /// @return A valid TextureHandle that will become readable once the upload drains.
        Rendering::Textures::TextureHandle SubmitTextureFile(uint8_t frame_index, uint8_t thread_index, const char* filename);

        /// @brief Return the (255, 20, 147) fallback TextureHandle for missing textures, creating it on first call.
        Rendering::Textures::TextureHandle GetOrCreateFallbackTexture();

        /// @brief Payload for a deferred texture upload.
        /// @details IsLarge = true stores an owned copy of the pixel data (std::vector<uint8_t>).
        ///          IsLarge = false stores a raw pointer valid until CompleteDeferrals runs.
        struct TextureDeferral
        {
            uint8_t                                            FrameIdx  = 0;
            uint8_t                                            ThreadIdx = 0;
            std::variant<unsigned char*, std::vector<uint8_t>> Buffer;
            Rendering::Textures::TextureHandle                 TexHandle = {};
            bool                                               IsLarge   = false;
        };

        /// @brief Enqueue a texture upload deferral for processing in the next BeginFrame.
        /// @param deferral Deferral to enqueue; ownership of the Buffer variant is transferred.
        void                             EnqueueTextureDeferral(TextureDeferral&& deferral);

        /// @brief Drain all pending texture deferrals by dispatching UploadTextureBuffer.
        /// @details Called from AppRenderPipeline::BeginFrame. Processes every entry in
        ///          m_tex_deferral_queue and submits the underlying staging copies.
        void                             CompleteDeferrals();

        /// @brief Submit all pending timeline semaphore jobs to the GPU graphics queue.
        /// @details Called from AppRenderPipeline::EndFrame. Processes m_tex_job_queue.
        void                             SubmitTextureJobs();

        /// @brief Retire command buffers whose timeline fence has been signalled.
        /// @details Frees staging buffers associated with completed texture uploads for the
        ///          given frame/thread pool. Called from AppRenderPipeline::BeginFrame.
        /// @param frame_index  Render frame index.
        /// @param thread_index Thread index within the per-frame pool.
        void                             RetireTextureSlots(uint8_t frame_index, uint8_t thread_index);

        /// @brief Cancel all queued timeline jobs without submitting them.
        /// @details Called on swapchain resize or recreate to discard stale uploads.
        void                             ClearTextureJobs();

        /// @brief Reset all texture timeline semaphore counters after a swapchain recreate.
        /// @details Re-initialises per-pool signal values and retire arrays to zero.
        void                             ResetTextureTimelines();

        /// @brief Schedule a hot-reload swap for a mesh buffer.
        /// @details Uploads the new version of new_asset and swaps it into old_handle after
        ///          FRAMES_IN_FLIGHT frames have drained, then queues the old buffer for deletion.
        /// @param old_handle The live BufferHandle to replace.
        /// @param new_asset  AssetHandle for the new version of the mesh.
        void                             ScheduleSwap(BufferHandle old_handle, Managers::AssetHandle new_asset);

        /// @brief Schedule a hot-reload swap for a texture image.
        /// @details Uploads the new version of new_asset and swaps it into old_handle after
        ///          FRAMES_IN_FLIGHT frames, then queues the old image for deletion.
        /// @param old_handle The live ImageHandle to replace.
        /// @param new_asset  AssetHandle for the new version of the texture.
        void                             ScheduleSwap(ImageHandle old_handle, Managers::AssetHandle new_asset);

        /// @brief Deferred release of a GPU buffer.
        /// @details For generic device-local buffers (handles returned by UploadBuffer) the
        ///          underlying VmaAllocation is freed after FRAMES_IN_FLIGHT frames via the
        ///          deferred-free queue. For mesh handles the slot is invalidated only — the
        ///          packed global buffer is append-only and reclaimed on shutdown.
        /// @param handle Handle returned by UploadMesh or UploadBuffer.
        void                             Release(BufferHandle handle);

        /// @brief Deferred release of a GPU image.
        /// @details The VmaAllocation and VkImageView are freed after FRAMES_IN_FLIGHT frames.
        /// @param handle Handle returned by UploadTexture.
        void                             Release(ImageHandle handle);

        /// @brief Look up a generic device-local buffer by handle.
        /// @details Valid only for handles returned by UploadBuffer. Mesh handles must use
        ///          GetMeshOffsets instead. The returned pointer is valid for the current frame
        ///          only — do not store it across BeginFrame calls.
        /// @param handle Handle returned by UploadBuffer.
        /// @return Pointer to the BufferView, or nullptr if the handle is invalid or stale.
        const Core::Memory::BufferView*  GetBuffer(BufferHandle handle) const;

        /// @brief Look up a GPU image by handle.
        /// @details The returned pointer is valid for the current frame only.
        /// @param handle Handle returned by UploadTexture.
        /// @return Pointer to the BufferImage, or nullptr if the handle is invalid or stale.
        const Core::Memory::BufferImage* GetImage(ImageHandle handle) const;

        /// @brief Return the shared device-local VkBuffer that holds all uploaded vertex data.
        /// @details All mesh uploads are appended sequentially at the watermark cursor.
        ///          Bound once to the VertexSB descriptor; per-draw vertex offset is in DrawData.
        /// @return Pointer to the global vertex BufferView; always valid after Initialize.
        const Core::Memory::BufferView*  GetGlobalVertexBuffer() const
        {
            return &m_global_vertex_buf;
        }

        /// @brief Return the shared device-local VkBuffer that holds all uploaded index data.
        /// @details Analogous to GetGlobalVertexBuffer for index (uint32) data.
        /// @return Pointer to the global index BufferView; always valid after Initialize.
        const Core::Memory::BufferView* GetGlobalIndexBuffer() const
        {
            return &m_global_index_buf;
        }

        /// @brief Return true once at least one mesh has been appended to the global buffers.
        /// @details Used by GraphicRenderer to defer global-buffer descriptor binding until
        ///          data is present.
        bool GlobalBuffersReady() const
        {
            return m_vtx_cursor > 0;
        }

        /// @brief Query the element-count offsets for a mesh in the global geometry buffers.
        /// @param handle     BufferHandle returned by UploadMesh.
        /// @param vtx_offset Out: index of the first DrawVertex element in the global VB.
        /// @param idx_offset Out: index of the first uint32 element in the global IB.
        /// @return true if the handle is valid and the offsets were written; false otherwise.
        bool         GetMeshOffsets(BufferHandle handle, uint32_t& vtx_offset, uint32_t& idx_offset) const;

        /// @brief Find the BufferHandle registered for a mesh asset by UUID.
        /// @details Returns an invalid handle if the mesh has not been uploaded yet or
        ///          the UUID is not in the uuid-to-buffer map.
        /// @param uuid Asset UUID from the meta file.
        /// @return Valid BufferHandle if found; invalid otherwise.
        BufferHandle FindMeshBuffer(const uuids::uuid& uuid) const;

        /// @brief Write CPU data into an existing HOST_VISIBLE BufferView.
        /// @details Uses the ring allocator for staging; falls back to a one-shot staging
        ///          buffer and RecordAndSubmit. Render-thread only.
        /// @param dst        Destination HOST_VISIBLE buffer (e.g. TransformSB, DrawDataSB).
        /// @param data       Source CPU data.
        /// @param byte_size  Number of bytes to write.
        /// @param dst_offset Byte offset within dst at which to begin writing.
        void         UpdateBuffer(Hardwares::BufferView& dst, const void* data, size_t byte_size, uint32_t dst_offset = 0);

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
        BufferHandle UploadBuffer(const void* data, size_t byte_size, VkBufferUsageFlags usage, const char* debug_name = nullptr);

        /// @brief Enqueue a VkShaderModule for timeline-gated deferred destruction.
        /// @details Used by shader hot-reload: call after all in-flight frames that reference
        ///          the old module have retired. The module is destroyed once the render
        ///          timeline semaphore advances past the current value.
        /// @param module The VkShaderModule to destroy; no-op if VK_NULL_HANDLE.
        void         EnqueueDeletion(VkShaderModule module);

        /// @brief Enqueue a VkPipeline for timeline-gated deferred destruction.
        /// @details Used by pipeline hot-reload and render graph rebuilds.
        ///          Safe to call immediately after switching to the new pipeline.
        /// @param pipeline The VkPipeline to destroy; no-op if VK_NULL_HANDLE.
        void         EnqueueDeletion(VkPipeline pipeline);

        /// @brief Enqueue a raw VkBuffer + VmaAllocation for deferred destruction.
        /// @details For buffers managed outside the RRM slot pools (e.g. scratch allocations).
        ///          Freed after FRAMES_IN_FLIGHT frames via the deferred-free queue.
        /// @param buffer     The VkBuffer to destroy; no-op if VK_NULL_HANDLE.
        /// @param allocation The associated VmaAllocation to free.
        void         EnqueueDeletion(VkBuffer buffer, VmaAllocation allocation);

        /// @brief Enqueue a VkImage + VkImageView + VmaAllocation for deferred destruction.
        /// @details For images managed outside the RRM slot pools.
        ///          Freed after FRAMES_IN_FLIGHT frames via the deferred-free queue.
        /// @param image      The VkImage to destroy; no-op if VK_NULL_HANDLE.
        /// @param view       The associated VkImageView to destroy.
        /// @param allocation The associated VmaAllocation to free.
        void         EnqueueDeletion(VkImage image, VkImageView view, VmaAllocation allocation);

        /// @brief Enqueue a pre-built DeferredFreeEntry for timeline-gated deferred destruction.
        /// @details Low-level overload for callers that construct the entry themselves.
        ///          Prefer the typed overloads above when possible.
        void         EnqueueDeletion(Hardwares::DeferredFreeEntry entry);

    private:
        enum class UploadKind : uint8_t
        {
            Mesh    = 0,
            Texture = 1,
        };

        struct PendingUpload
        {
            UploadKind            Kind;
            Managers::AssetHandle Asset;
            uuids::uuid           UUID;
        };

        enum class SwapKind : uint8_t
        {
            Buffer = 0,
            Image  = 1,
        };

        struct SwapEntry
        {
            SwapKind Kind;
            union
            {
                BufferHandle OldBuffer;
                ImageHandle  OldImage;
            };
            union
            {
                Core::Memory::BufferView  NewBuffer;
                Core::Memory::BufferImage NewImage;
            };
            uint32_t SwapSafeFrame = 0;

            SwapEntry() : Kind(SwapKind::Buffer), OldBuffer{}, NewBuffer{} {}
        };

        template <typename Resource>
        struct Slot
        {
            Resource Data       = {};
            uint32_t Generation = 0; // 0 = free
        };

        static constexpr uint32_t MAX_BUFFERS      = 4096;
        static constexpr uint32_t MAX_IMAGES       = 4096;
        static constexpr uint32_t MAX_GENERIC_BUFS = 4096;
        // Generation tag: bit 31 = 1 marks a generic-buffer handle so Release() and
        // GetBuffer() can distinguish them from mesh handles (bit 31 = 0).
        static constexpr uint32_t GBUF_GEN_TAG     = 0x8000'0000u;

        // Per-mesh record in the slot pool — element-count offsets into the global buffers.
        struct MeshSlot
        {
            uint32_t VtxOffset = 0; // first DrawVertex element in m_global_vertex_buf
            uint32_t IdxOffset = 0; // first uint32 element in m_global_index_buf
            uint32_t VtxCount  = 0;
            uint32_t IdxCount  = 0;
        };

        BufferHandle                    DoUploadMesh(Managers::AssetHandle asset, uint32_t frame_index);
        ImageHandle                     DoUploadTexture(Managers::AssetHandle asset);

        void                            AppendToGlobalBuffer(Core::Memory::BufferView& global_buf, const void* data, size_t byte_size, VkDeviceSize byte_offset, uint32_t frame_index);

        void                            FlushPendingUploads(uint32_t frame_index);

        Core::Memory::BufferImage*      GetImageMutable(ImageHandle handle);

        void                            InitUploadPool();
        void                            InitGlobalBuffers();
        uint32_t                        AllocMeshSlot();
        uint32_t                        AllocImageSlot();
        uint32_t                        AllocGBufSlot();

        Hardwares::VulkanDevice*        m_device                       = nullptr;
        Core::VFS::AssetRegistry*       m_registry                     = nullptr;

        // Global geometry buffers — all mesh vertices/indices packed together.
        static constexpr VkDeviceSize   GLOBAL_VTX_CAPACITY            = 256 * 1024 * 1024; // 256 MB → ~8M DrawVertex
        static constexpr VkDeviceSize   GLOBAL_IDX_CAPACITY            = 256 * 1024 * 1024; // 256 MB → ~64M uint32
        Core::Memory::BufferView        m_global_vertex_buf            = {};
        Core::Memory::BufferView        m_global_index_buf             = {};
        VkDeviceSize                    m_vtx_cursor                   = 0; // byte offset of next write
        VkDeviceSize                    m_idx_cursor                   = 0;

        // Mesh slot pool — stores per-mesh offsets into the global buffers.
        Slot<MeshSlot>                  m_mesh_slots[MAX_BUFFERS]      = {};
        uint32_t                        m_mesh_slot_count              = 0;

        // Image pool
        Slot<Core::Memory::BufferImage> m_image_slots[MAX_IMAGES]      = {};
        uint32_t                        m_image_slot_count             = 0;

        // Generic device-local buffer pool — for bone matrices, particle VBs, etc.
        // Handles carry GBUF_GEN_TAG in bit 31 to distinguish from mesh handles.
        Slot<Core::Memory::BufferView>  m_gbuf_slots[MAX_GENERIC_BUFS] = {};
        uint32_t                        m_gbuf_slot_count              = 0;

        // UUID → handle maps (for hot-reload swap lookup)
        // Written on first upload; read on OnAssetStale. Protected by m_uuid_map_mutex.
        struct UUIDBufferPair
        {
            uuids::uuid  UUID;
            BufferHandle Handle;
        };
        struct UUIDImagePair
        {
            uuids::uuid UUID;
            ImageHandle Handle;
        };
        static constexpr uint32_t      MAX_UUID_MAP                   = 4096;
        UUIDBufferPair                 m_uuid_to_buffer[MAX_UUID_MAP] = {};
        uint32_t                       m_uuid_to_buffer_count         = 0;
        UUIDImagePair                  m_uuid_to_image[MAX_UUID_MAP]  = {};
        uint32_t                       m_uuid_to_image_count          = 0;

        // Swap list — written from asset thread, drained in EndFrame
        static constexpr uint32_t      MAX_SWAPS                      = 256;
        SwapEntry                      m_swaps[MAX_SWAPS]             = {};
        uint32_t                       m_swap_count                   = 0;

        // Pending uploads — written from asset thread, flushed in BeginFrame
        static constexpr uint32_t      MAX_PENDING                    = 256;
        PendingUpload                  m_pending[MAX_PENDING]         = {};
        uint32_t                       m_pending_count                = 0;

        uint32_t                       m_current_frame                = 0;

        // Dedicated command pools for geometry uploads — isolated from the swapchain
        // timeline semaphore chain. Geometry uploads must not share the graphics queue
        // submission path with Present; a private pool + fence ensures safe isolation.
        Rendering::Pools::CommandPool* m_upload_pool                  = nullptr;
        Hardwares::CommandBuffer*      m_upload_cmd                   = nullptr;
        VkFence                        m_upload_fence                 = VK_NULL_HANDLE;
        Rendering::Pools::CommandPool* m_transfer_pool                = nullptr;
        Hardwares::CommandBuffer*      m_transfer_cmd                 = nullptr;
        VkFence                        m_transfer_fence               = VK_NULL_HANDLE;

        // Upper bound for texture timeline slot search — keeps textures out of the
        // geometry slots and caps the retire loop to the same range.
        static constexpr uint32_t      GEOMETRY_UPLOAD_SLOT           = 15;

        struct TextureTimelineJob
        {
            Hardwares::CommandBuffer*         Buffer       = nullptr;
            Rendering::Primitives::Semaphore* Timeline     = nullptr;
            Rendering::Primitives::Semaphore* WaitTimeline = nullptr;
            uint32_t                          WaitFlag     = 0;
            uint64_t                          SignalValue  = 0;
            uint64_t                          WaitValue    = UINT64_MAX;
        };

        uint32_t                                                                   m_tex_total_cmd_count      = 0;
        Core::Containers::Array<std::atomic_uint64_t>                              m_tex_next_values          = {};
        Core::Containers::Array<std::atomic_uint64_t>                              m_tex_transfer_next_values = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*>                 m_tex_timelines            = {};
        Core::Containers::Array<Rendering::Primitives::Semaphore*>                 m_tex_transfer_timelines   = {};
        Core::Containers::Array<Core::Containers::Array<uint64_t>>                 m_tex_retire_values        = {};
        Core::Containers::Array<Core::Containers::Array<uint64_t>>                 m_tex_transfer_retire      = {};
        Core::Containers::Array<Core::Containers::Array<Core::Memory::BufferView>> m_tex_retire_staging       = {};
        Core::Containers::Array<Core::Containers::Array<Core::Memory::BufferView>> m_tex_transfer_staging     = {};
        Helpers::ThreadSafeQueue<TextureTimelineJob>                               m_tex_job_queue            = {};
        Helpers::ThreadSafeQueue<TextureDeferral>                                  m_tex_deferral_queue       = {};

        void                                                                       InitTextureTimelines();
        void                                                                       ShutdownTextureTimelines();

        std::mutex                                                                 m_pending_mutex;
        std::mutex                                                                 m_uuid_map_mutex;
    };

} // namespace ZEngine::Rendering
