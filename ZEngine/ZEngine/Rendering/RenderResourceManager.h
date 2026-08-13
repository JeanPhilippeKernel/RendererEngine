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

        void                               Initialize(Hardwares::VulkanDevice* device, Core::VFS::AssetRegistry* registry);
        void                               Shutdown();

        // Called once per frame from the render thread (AppRenderPipeline::BeginFrame).
        // Flushes pending uploads queued from the asset thread.
        void                               BeginFrame(uint32_t frame_index);

        // Called once per frame from the render thread (AppRenderPipeline::EndFrame).
        // Drains swap entries that have aged past FRAMES_IN_FLIGHT.
        void                               EndFrame(uint32_t frame_index);

        // Upload mesh/texture from an AssetHandle to a device-local GPU resource.
        // Returns an invalid handle on failure (out of memory, null asset, etc.).
        // Safe to call from any thread — work is queued and flushed in BeginFrame.
        BufferHandle                       UploadMesh(Managers::AssetHandle asset_handle);
        ImageHandle                        UploadTexture(Managers::AssetHandle asset_handle);

        // Direct texture upload from raw pixel data (e.g. procedural textures).
        // Submits via timeline job queue; submission drains in SubmitTextureJobs().
        Rendering::Textures::TextureHandle UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data);

        // Create the font atlas texture and enqueue an owned-copy deferral.
        // The pixel data is copied immediately so the caller may free its buffer after
        // this call returns. The GPU upload is dispatched by CompleteDeferrals on the
        // next BeginFrame. Caller must enqueue the returned handle to
        // TextureHandleToUpdates for bindless descriptor registration.
        Rendering::Textures::TextureHandle UploadFontAtlas(unsigned char* pixels, uint32_t width, uint32_t height);

        // Load a texture file from disk, decode it on the thread pool, and upload to GPU.
        Rendering::Textures::TextureHandle SubmitTextureFile(uint8_t frame_index, uint8_t thread_index, const char* filename);

        // Queue a deferred texture upload (for large textures or cross-frame deferral).
        struct TextureDeferral
        {
            uint8_t                                            FrameIdx  = 0;
            uint8_t                                            ThreadIdx = 0;
            std::variant<unsigned char*, std::vector<uint8_t>> Buffer;
            Rendering::Textures::TextureHandle                 TexHandle = {};
            bool                                               IsLarge   = false;
        };
        void                             EnqueueTextureDeferral(TextureDeferral&& deferral);

        // Drain deferred texture uploads queued via EnqueueTextureDeferral.
        // Called from AppRenderPipeline::BeginFrame.
        void                             CompleteDeferrals();

        // Submit all pending timeline semaphore jobs to the GPU queue.
        // Called from AppRenderPipeline::EndFrame.
        void                             SubmitTextureJobs();

        // Retire completed command buffers for a given frame/thread pool.
        // Called from AppRenderPipeline::BeginFrame on each pool.
        void                             RetireTextureSlots(uint8_t frame_index, uint8_t thread_index);

        // Cancel all queued timeline jobs (called on swapchain resize/recreate).
        void                             ClearTextureJobs();

        // Reset timeline semaphore counters for all pools after a swapchain recreate.
        void                             ResetTextureTimelines();

        // Hot-reload: upload new version, swap after FRAMES_IN_FLIGHT frames drain.
        void                             ScheduleSwap(BufferHandle old_handle, Managers::AssetHandle new_asset);
        void                             ScheduleSwap(ImageHandle old_handle, Managers::AssetHandle new_asset);

        // Deferred release — actual GPU memory freed after FRAMES_IN_FLIGHT frames.
        void                             Release(BufferHandle handle);
        void                             Release(ImageHandle handle);

        // Read-only accessors — valid only for the current frame on the render thread.
        const Core::Memory::BufferImage* GetImage(ImageHandle handle) const;

        // Global geometry buffers — one VkBuffer for all vertex data, one for all index data.
        // All mesh uploads are appended at the current watermark cursor.
        // VertexSB/IndexSB descriptors bind these once; DrawData offsets select the right region.
        const Core::Memory::BufferView*  GetGlobalVertexBuffer() const
        {
            return &m_global_vertex_buf;
        }
        const Core::Memory::BufferView* GetGlobalIndexBuffer() const
        {
            return &m_global_index_buf;
        }
        bool GlobalBuffersReady() const
        {
            return m_vtx_cursor > 0;
        }

        // Look up the element-count offsets for a mesh already uploaded to the global buffers.
        // vtx_offset = first DrawVertex element index; idx_offset = first uint32 index element.
        bool         GetMeshOffsets(BufferHandle handle, uint32_t& vtx_offset, uint32_t& idx_offset) const;

        // Look up the vertex buffer handle registered for a mesh asset UUID.
        // Returns an invalid handle if the mesh has not been uploaded yet.
        BufferHandle FindMeshBuffer(const uuids::uuid& uuid) const;

        // Upload / update buffer contents from CPU data.
        // Uses RRM's dedicated command pool + Ring staging. Render-thread only.
        void         UpdateBuffer(Hardwares::BufferView& dst, const void* data, size_t byte_size, uint32_t dst_offset = 0);

        // Enqueue a raw Vulkan object for timeline-gated deferred destruction.
        // Used by shader hot-reload and pipeline rebuild paths.
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

        static constexpr uint32_t MAX_BUFFERS = 4096;
        static constexpr uint32_t MAX_IMAGES  = 4096;

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

        Hardwares::VulkanDevice*        m_device                  = nullptr;
        Core::VFS::AssetRegistry*       m_registry                = nullptr;

        // Global geometry buffers — all mesh vertices/indices packed together.
        static constexpr VkDeviceSize   GLOBAL_VTX_CAPACITY       = 256 * 1024 * 1024; // 256 MB → ~8M DrawVertex
        static constexpr VkDeviceSize   GLOBAL_IDX_CAPACITY       = 256 * 1024 * 1024; // 256 MB → ~64M uint32
        Core::Memory::BufferView        m_global_vertex_buf       = {};
        Core::Memory::BufferView        m_global_index_buf        = {};
        VkDeviceSize                    m_vtx_cursor              = 0; // byte offset of next write
        VkDeviceSize                    m_idx_cursor              = 0;

        // Mesh slot pool — stores per-mesh offsets into the global buffers.
        Slot<MeshSlot>                  m_mesh_slots[MAX_BUFFERS] = {};
        uint32_t                        m_mesh_slot_count         = 0;

        // Image pool
        Slot<Core::Memory::BufferImage> m_image_slots[MAX_IMAGES] = {};
        uint32_t                        m_image_slot_count        = 0;

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
