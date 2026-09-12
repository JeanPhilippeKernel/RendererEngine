#pragma once
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct IPipeline;
    class PSOCache;
} // namespace ZEngine::Rendering::Renderers::Pipelines
namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct GraphicPass;
}

// clang-format off
#include <ZEngine/Core/Containers/SPSCQueue.h>
#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/RenderHandle.h>
#include <ZEngine/Hardwares/CommandBufferManager.h>
#include <ZEngine/Hardwares/DeferredFreeQueue.h>
#include <ZEngine/Hardwares/DeviceSwapchain.h>
#include <ZEngine/Hardwares/PerFrameUploadHeap.h>
#include <ZEngine/Hardwares/VulkanLayer.h>
#include <ZEngine/Helpers/HandleManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Primitives/Fence.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <ZEngine/Rendering/Pools/CommandPool.h>
#include <ZEngine/Rendering/Primitives/ImageMemoryBarrier.h>
#include <ZEngine/Rendering/ResourceTypes.h>
#include <ZEngine/Rendering/Specifications/ShaderSpecification.h>
#include <ZEngine/Rendering/Specifications/RenderPassSpecification.h>
#include <ZEngine/Rendering/Textures/Texture.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashSet.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <limits>
#include <cstdint>
#include <atomic>
// clang-format on

namespace ZEngine::Windows
{
    class CoreWindow;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass;
    struct Attachment;
} // namespace ZEngine::Rendering::Renderers::RenderPasses

namespace ZEngine::Rendering::Shaders
{
    struct Shader;
}

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline;
}

namespace ZEngine::Hardwares
{
    using Core::Memory::BufferImage;
    using Core::Memory::BufferView;
    using Core::Memory::GpuMemoryDomain;

    struct WriteDescriptorSetRequestKey;
    struct AsyncGPUOperation;
    struct AsyncGPUOperationHandle;

    inline constexpr uint32_t kMaxShaderReloadPathBytes = 512;

    /// @brief Cross-thread request consumed by the render thread before command recording.
    struct ShaderReloadRequest
    {
        char Path[kMaxShaderReloadPathBytes] = {};
    };
    /*
     * GPU Device
     */
    struct VulkanDevice;

    struct ImageBuffer
    {
        struct CachedImageView
        {
            VkImageSubresourceRange Range    = {};
            VkImageViewType         ViewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
            VkImageView             Handle   = VK_NULL_HANDLE;
        };

        ImageBuffer() = default;
        ~ImageBuffer();

        Rendering::Specifications::ImageLayout              Layout        = Rendering::Specifications::ImageLayout::UNDEFINED;
        Rendering::Specifications::ImageBufferSpecification Specification = {};
        cstring                                             DebugName     = nullptr;
        VulkanDevice*                                       Device        = nullptr;

        void                                                Construct(VulkanDevice* device);
        /// @brief Constructs a distinct VkImage backed by `backing`'s VMA allocation.
        void                                                ConstructAliasing(VulkanDevice* device, const BufferImage& backing);

        BufferImage&                                        GetBuffer();
        const BufferImage&                                  GetBuffer() const;
        VkImageView                                         GetImageViewHandle() const;
        /// @brief Returns a lazily-created view for one exact image subresource range.
        /// @details The returned handle remains valid until this ImageBuffer is reconstructed or disposed.
        VkImageView                                         GetImageViewHandle(VkImageViewType view_type, const VkImageSubresourceRange& range);
        VkImage                                             GetHandle() const;
        VkSampler                                           GetSampler() const;
        void                                                Dispose();
        VkDescriptorImageInfo&                              GetDescriptorImageInfo();

    private:
        BufferImage                              m_buffer_image;
        VkDescriptorImageInfo                    m_image_info;
        Core::Containers::Array<CachedImageView> m_cached_image_views;
    };

    struct QueueView
    {
        uint32_t FamilyIndex{0xFFFFFFFF};
        VkQueue  Handle{VK_NULL_HANDLE};
    };

    /// @brief Element of TextureHandleToDispose; TimelineValue gates Present()'s drain.
    struct TextureDisposeEntry
    {
        Rendering::Textures::TextureHandle Handle        = {};
        uint64_t                           TimelineValue = 0;
    };

    /// @brief Lock-free SPSC ring for TextureHandleToDispose — producer and consumer are
    ///        both render-thread only, so no mutex is needed.
    using TextureDisposeQueue = Core::Containers::SPSCQueue<TextureDisposeEntry, 1024>;

    /*
     * Command Buffer definition
     */
    enum CommandBufferState : uint8_t
    {
        Idle = 0,
        Recording,
        Executable,
        Pending,
        Invalid
    };
    enum CommandBufferType : uint8_t
    {
        Primary = 0,
        Secondary
    };

    struct CommandBuffer
    {
        /// @brief Creates a Vulkan command buffer and its transient recording arena.
        /// @param parent_arena Optional persistent arena for the recording arena.
        /// @details Render-graph buffers pass a manager-owned arena because they
        /// must outlive RenderGraph::Execute()'s Device->Arena scratch scope.
        CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool primary, Core::Memory::ArenaAllocator* parent_arena = nullptr);
        ~CommandBuffer();

        Rendering::QueueType              QueueType;
        CommandBufferType                 BufferType = CommandBufferType::Primary;
        Hardwares::VulkanDevice*          Device     = nullptr;
        Core::Memory::ArenaAllocator      LocalArena = {};

        void                              Create();
        void                              Free();
        VkCommandBuffer                   GetHandle() const;
        void                              Begin();
        void                              BeginSecondary(Rendering::Renderers::RenderPasses::GraphicPass* const render_pass, VkFramebuffer framebuffer);
        /// @brief Begins a secondary command buffer outside a rendering instance.
        void                              BeginSecondaryCompute();
        void                              End();
        bool                              Completed();
        bool                              IsExecutable();
        bool                              IsRecording();
        CommandBufferState                GetState() const;
        void                              ResetState();
        void                              SetState(const CommandBufferState& state);
        void                              SetSignalFence(Rendering::Primitives::Fence* const semaphore);
        void                              SetSignalSemaphore(Rendering::Primitives::Semaphore* const semaphore);
        Rendering::Primitives::Semaphore* GetSignalSemaphore() const;
        Rendering::Primitives::Fence*     GetSignalFence();
        /// @brief Begins a dynamic-rendering instance on this primary command buffer.
        void                              BeginDynamicRendering(const VkRenderingInfo& rendering_info);
        /// @brief Ends the active dynamic-rendering instance on this command buffer.
        void                              EndDynamicRendering();
        /// @brief Transitions the acquired swapchain image for color-attachment use.
        void                              TransitionSwapchainImageToColorAttachment();
        /// @brief Transitions the acquired swapchain image to the presentation layout.
        void                              TransitionSwapchainImageToPresent();
        void                              BeginRenderPass(Rendering::Renderers::RenderPasses::GraphicPass* const, VkFramebuffer framebuffer, bool is_content_secondary_command_buffer);
        void                              EndRenderPass();
        void                              BindDescriptorSets(uint32_t frame_index = 0, const uint32_t* dynamic_offsets = nullptr, uint32_t dynamic_offset_count = 0);
        void                              BindDescriptorSet(const VkDescriptorSet& descriptor);
        void                              BindPipeline(Rendering::Renderers::Pipelines::IPipeline* const pipeline);
        void                              DrawIndirect(VkBuffer buffer, uint32_t offset, uint32_t draw_count);
        void                              DrawIndexedIndirect(VkBuffer buffer, uint32_t offset, uint32_t count);
        void                              Dispatch(uint32_t group_count_x, uint32_t group_count_y = 1, uint32_t group_count_z = 1);
        void                              DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
        void                              Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance);
        /// @brief Records a Vulkan Synchronization2 dependency on this command buffer.
        void                              PipelineBarrier2(const VkDependencyInfo& dependency_info);
        /// @brief Begins conditional execution using a GPU-written 32-bit predicate.
        void                              BeginConditionalRendering(VkBuffer condition_buffer, VkDeviceSize offset = 0, bool invert = false);
        /// @brief Ends the current conditional-execution scope.
        void                              EndConditionalRendering();
        /// @brief Records a Synchronization2 timestamp query on this command buffer.
        void                              WriteTimestamp2(VkPipelineStageFlags2 stage_mask, VkQueryPool query_pool, uint32_t query);
        /// @brief Resets a query range before it is reused by this command buffer.
        /// @details RenderGraph records this on the graphics primary after the
        /// associated FrameContext fence has completed.
        void                              ResetQueryPool(VkQueryPool query_pool, uint32_t first_query, uint32_t query_count);
        /// @brief Begins an occlusion or pipeline-statistics query.
        void                              BeginQuery(VkQueryPool query_pool, uint32_t query, VkQueryControlFlags flags = 0);
        /// @brief Ends an active occlusion or pipeline-statistics query.
        void                              EndQuery(VkQueryPool query_pool, uint32_t query);
        /// @brief Copies query-pool results into a transfer-destination buffer.
        void                              CopyQueryPoolResults(VkQueryPool query_pool, uint32_t first_query, uint32_t query_count, VkBuffer destination, VkDeviceSize destination_offset, VkDeviceSize stride, VkQueryResultFlags flags);
        void                              BeginDebugLabel(cstring name);
        void                              EndDebugLabel();
        void                              TransitionImageLayout(const Rendering::Primitives::ImageMemoryBarrier& image_barrier);
        /// @brief Copies a graph-declared range between two Vulkan buffers.
        /// @details The caller declares transfer synchronization through the render graph;
        /// this wrapper intentionally records only vkCmdCopyBuffer.
        void                              CopyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size, VkDeviceSize source_offset = 0, VkDeviceSize destination_offset = 0);
        void                              CopyBufferToImage(const Hardwares::BufferView& source, Hardwares::BufferImage& destination, uint32_t width, uint32_t height, uint32_t layer_count, VkImageLayout new_layout, uint32_t source_offset = 0);
        void                              BindVertexBuffer(const Core::Memory::BufferView& buffer);
        void                              BindIndexBuffer(const Core::Memory::BufferView& buffer, VkIndexType type);
        void                              SetScissor(uint32_t w, uint32_t h, int32_t x = 0, int32_t y = 0);
        void                              SetViewport(uint32_t w, uint32_t h, float x = 0.0f, float y = 0.0f, float min_depth = 0.0f, float max_depth = 1.0f);
        void                              PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data);
        /// @brief Executes one already-recorded secondary command buffer without allocation.
        void                              ExecuteSecondaryCommandBuffer(CommandBuffer* const buffer);
        void                              ExecuteSecondaryCommandBuffers(Core::Containers::ArrayView<CommandBuffer> buffers);

    private:
        std::atomic_uint8_t                         m_command_buffer_state{CommandBufferState::Idle};
        VkCommandBuffer                             m_command_buffer{VK_NULL_HANDLE};
        VkCommandPool                               m_command_pool{VK_NULL_HANDLE};
        Rendering::Primitives::Fence*               m_signal_fence                = nullptr;
        Rendering::Primitives::Semaphore*           m_signal_semaphore            = nullptr;
        Rendering::Renderers::Pipelines::IPipeline* m_active_pipeline             = nullptr;
        bool                                        m_in_render_pass              = false;
        bool                                        m_in_dynamic_rendering        = false;
        bool                                        m_dynamic_swapchain_rendering = false;
        bool                                        m_in_conditional_rendering    = false;
    };

    ZDEFINE_PTR(CommandBuffer);

    struct WriteDescriptorSetRequestKey
    {
        uint32_t        Binding = 0;
        VkDescriptorSet DstSet  = VK_NULL_HANDLE;

        bool            operator==(const WriteDescriptorSetRequestKey& other) const
        {
            return Binding == other.Binding && DstSet == other.DstSet;
        }
    };

    /*
     * Async GPU operation handle and definition
     */
    struct AsyncGPUOperationHandle
    {
        VkPipelineStageFlags2             StageFlags  = 0;
        uint64_t                          SignalValue = 0;
        Rendering::Primitives::Semaphore* Timeline    = nullptr;
    };

    struct AsyncGPUOperation
    {
        uint64_t                          NextValue    = 0;
        Rendering::Primitives::Semaphore* Timeline     = nullptr;
        Core::Containers::Array<uint64_t> RetireValues = {};

        void                              Initialize(VulkanDevice* device, uint32_t total_buffer_count);
    };
    /*
     *  Device definition
     */
    struct VulkanDevice
    {
        bool                                                                                                                         HasSeperateTransfertQueueFamily                                             = false;
        bool                                                                                                                         HasSeparateComputeQueueFamily                                               = false;
        /// @brief True when transfer work has a distinct VkQueue handle.
        bool                                                                                                                         HasSeparateTransferQueue                                                    = false;
        /// @brief True when compute work has a distinct VkQueue handle.
        bool                                                                                                                         HasSeparateComputeQueue                                                     = false;
        bool                                                                                                                         PhysicalDeviceSupportSampledImageBindless                                   = false;
        bool                                                                                                                         PhysicalDeviceSupportStorageBufferBindless                                  = false;
        bool                                                                                                                         PhysicalDeviceSupportTimelineSemaphore                                      = false;
        bool                                                                                                                         PhysicalDeviceSupportDynamicRendering                                       = false;
        /// @brief True only when host-side timestamp query resets were enabled.
        bool                                                                                                                         PhysicalDeviceSupportHostQueryReset                                         = false;
        /// @brief True when VK_EXT_conditional_rendering was enabled on this device.
        bool                                                                                                                         PhysicalDeviceSupportConditionalRendering                                   = false;
        /// @brief Valid timestamp bits for every resolved queue role; zero disables that role.
        uint32_t                                                                                                                     QueueTimestampValidBits[static_cast<uint32_t>(Rendering::QueueType::COUNT)] = {};
        // Sticky, set once VK_ERROR_DEVICE_LOST is observed on any queue/swapchain call —
        // see CheckDeviceLost. The render loop checks this and stops issuing further Vulkan
        // calls: once a device is lost, continuing to call into it is undefined behaviour and
        // has been observed to segfault inside the Vulkan loader rather than return cleanly.
        std::atomic_bool                                                                                                             IsDeviceLost                                                                = false;
        const char*                                                                                                                  ApplicationName                                                             = "Tetragrama";
        const char*                                                                                                                  EngineName                                                                  = "ZEngine";
        uint32_t                                                                                                                     WorkerThreadCount                                                           = 1;
        uint32_t                                                                                                                     GraphicFamilyIndex                                                          = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                                                                     TransferFamilyIndex                                                         = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                                                                     ComputeFamilyIndex                                                          = std::numeric_limits<uint32_t>::max();
        uint32_t                                                                                                                     GraphicQueueIndex                                                           = 0;
        uint32_t                                                                                                                     TransferQueueIndex                                                          = 0;
        uint32_t                                                                                                                     ComputeQueueIndex                                                           = 0;

        uint32_t                                                                                                                     WriteDescriptorSetIndex                                                     = 0;
        uint32_t                                                                                                                     MaxGlobalTexture                                                            = 8192;
        /// @brief Geometry streaming pool budget in bytes (vtx + idx combined).
        ///        0 = auto-detect from PhysicalDeviceMemoryProperties (default).
        ///        Set by Engine::Initialize from project.json memory.geometry_streaming_mb
        ///        before RenderResourceManager::Initialize runs.
        VkDeviceSize                                                                                                                 GeometryStreamingBudget                                                     = 0;
        VkInstance                                                                                                                   Instance                                                                    = VK_NULL_HANDLE;
        VkSurfaceKHR                                                                                                                 Surface                                                                     = VK_NULL_HANDLE;
        VkSurfaceFormatKHR                                                                                                           SurfaceFormat                                                               = {};
        VkPresentModeKHR                                                                                                             PresentMode                                                                 = {};
        VkPhysicalDeviceProperties2                                                                                                  PhysicalDeviceProperties                                                    = {};
        VkPhysicalDeviceVulkan12Properties                                                                                           PhysicalDeviceVulkan12Properties                                            = {};
        VkDevice                                                                                                                     LogicalDevice                                                               = VK_NULL_HANDLE;
        VkPhysicalDevice                                                                                                             PhysicalDevice                                                              = VK_NULL_HANDLE;
        VkPhysicalDeviceFeatures2                                                                                                    PhysicalDeviceFeature                                                       = {};
        VkPhysicalDeviceMemoryProperties                                                                                             PhysicalDeviceMemoryProperties                                              = {};
        VkSampler                                                                                                                    GlobalLinearWrapSampler                                                     = VK_NULL_HANDLE;
        VkSampler                                                                                                                    GlobalLinearClampToEdgeSampler                                              = VK_NULL_HANDLE;
        VkDescriptorPool                                                                                                             GlobalDescriptorPoolHandle                                                  = VK_NULL_HANDLE;
        VkDescriptorSetLayout                                                                                                        EmptyDescriptorSetLayout                                                    = VK_NULL_HANDLE;
        VkDescriptorPool                                                                                                             EmptyDescriptorPoolHandle                                                   = VK_NULL_HANDLE;
        VkDescriptorSet                                                                                                              EmptyDescriptorSet                                                          = VK_NULL_HANDLE;
        Core::Memory::GpuAllocator                                                                                                   GpuMem                                                                      = {};
        DeferredFreeQueue                                                                                                            PendingFree                                                                 = {};
        PerFrameUploadHeap                                                                                                           FrameHeaps[3]                                                               = {};
        VkDescriptorImageInfo                                                                                                        GlobalLinearWrapSamplerImageInfo                                            = {};
        VkDescriptorImageInfo                                                                                                        GlobalLinearClampToEdgeSamplerImageInfo                                     = {};
        CommandBufferManagerPtr                                                                                                      CommandBufferMgr                                                            = {};
        DeviceSwapchainPtr                                                                                                           SwapchainPtr                                                                = {};
        Core::Containers::Array<VkFormat>                                                                                            DefaultDepthFormats                                                         = {};

        Core::Containers::UnorderedHashMap<const char*, Helpers::Handle<Rendering::Shaders::Shader>>                                 ShaderCaches                                                                = {};
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<VkDescriptorSet>>                                       ShaderReservedDescriptorSetMap                                              = {}; //<set, vec<descriptorSet>>
        Core::Containers::UnorderedHashMap<uint32_t, VkDescriptorSetLayout>                                                          ShaderReservedDescriptorSetLayoutMap                                        = {}; // <set, layout>
        Core::Containers::UnorderedHashMap<uint32_t, Core::Containers::Array<Rendering::Specifications::LayoutBindingSpecification>> ShaderReservedLayoutBindingSpecificationMap                                 = {};
        Core::Containers::Array<WriteDescriptorSetRequestKey>                                                                        BindlessTextureSlotRequests                                                 = {};
        Core::Containers::HashSet<uint32_t>                                                                                          ShaderReservedBindingSets                                                   = {};
        Rendering::Textures::TextureHandleManager                                                                                    GlobalTextures                                                              = {};
        Helpers::HandleManager<ImageBuffer>                                                                                          ImageBufferManager                                                          = {};
        Core::Containers::MPSCQueue<Rendering::Textures::TextureHandle, 8192>                                                        TextureHandleToUpdates                                                      = {};
        Core::Containers::SPSCQueue<Rendering::Textures::TextureHandle, 128>                                                         DeferredTextureDescriptorUpdates                                            = {};
        TextureDisposeQueue                                                                                                          TextureHandleToDispose                                                      = {};
        Core::Containers::MPSCQueue<AsyncGPUOperationHandle, 256>                                                                    AsyncGPUOperations                                                          = {};
        Core::Containers::SPSCQueue<AsyncGPUOperationHandle, 128>                                                                    DeferredAsyncGPUOperations                                                  = {};
        Core::Containers::MPSCQueue<ShaderReloadRequest, 64>                                                                         ShaderReloadRequests                                                        = {};
        VkDescriptorImageInfo                                                                                                        FallbackDescriptorImageInfo                                                 = {};
        Helpers::HandleManager<Rendering::Shaders::Shader>                                                                           ShaderManager                                                               = {};
        /// @brief Device-owned cache for descriptor layouts, pipeline layouts, and driver pipeline data.
        Rendering::Renderers::Pipelines::PSOCache*                                                                                   PipelineStateCache                                                          = nullptr;
        std::mutex                                                                                                                   Mutex                                                                       = {};
        Windows::CoreWindow*                                                                                                         CurrentWindow                                                               = nullptr;
        ZEngine::Core::Memory::ArenaAllocator*                                                                                       Arena                                                                       = nullptr;
        void*                                                                                                                        RRM                                                                         = nullptr;

        void                                                                                                                         Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::CoreWindow* const window, uint32_t worker_thread_count);
        void                                                                                                                         Deinitialize();
        void                                                                                                                         Dispose();
        bool                                                                                                                         QueueSubmit(CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, VkPipelineStageFlags2 wait_flag, uint64_t signal_value, uint64_t wait_value, Rendering::Primitives::Semaphore* const wait_timeline);
        bool                                                                                                                         QueueSubmit(CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, uint64_t signal_value, const VkSemaphoreSubmitInfo* wait_infos, uint32_t wait_info_count);
        bool                                                                                                                         QueueSubmit(const VkPipelineStageFlags wait_stage_flag, CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore = nullptr, Rendering::Primitives::Fence* const fence = nullptr);
        /// @brief If result is VK_ERROR_DEVICE_LOST, sets IsDeviceLost (logging once, on the
        ///        first caller to observe it) and returns true so the caller can bail out
        ///        instead of asserting or continuing to submit to a dead device.
        /// @param where Short description of the call site, for the one-time log line.
        bool                                                                                                                         CheckDeviceLost(VkResult result, const char* where);
        void                                                                                                                         EnqueueAsyncGPUOperation(const AsyncGPUOperationHandle& handle);
        void                                                                                                                         EnqueueDeferredAsyncGPUOperation(const AsyncGPUOperationHandle& handle);
        /// @brief Registers a bindless descriptor target once for subsequent texture updates.
        void                                                                                                                         AddBindlessTextureSlotRequest(const WriteDescriptorSetRequestKey& request);
        QueueView                                                                                                                    GetQueue(Rendering::QueueType type);
        void                                                                                                                         QueueWait(Rendering::QueueType type);
        void                                                                                                                         QueueWaitAll();
        // Optional VK_EXT_debug_utils instrumentation. These are no-ops when the
        // extension is unavailable, so render-graph execution stays portable.
        void                                                                                                                         BeginDebugLabel(VkCommandBuffer command_buffer, cstring name) const;
        void                                                                                                                         EndDebugLabel(VkCommandBuffer command_buffer) const;
        /// @brief Assigns a tooling-only Vulkan object name when debug-utils is available.
        void                                                                                                                         SetDebugObjectName(VkObjectType object_type, uint64_t object_handle, cstring name) const;
        void                                                                                                                         MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data);
        BufferView                                                                                                                   CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, Core::Memory::GpuMemoryDomain domain, const char* debug_name = nullptr);
        /// @brief Creates a distinct buffer object backed by an existing allocation.
        BufferView                                                                                                                   CreateAliasingBuffer(const BufferView& backing, VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, const char* debug_name = nullptr);
        void                                                                                                                         TickMemory();
        void                                                                                                                         DeferFree(DeferredFreeEntry entry);
        uint32_t                                                                                                                     MinUniformBufferOffsetAlignment() const;
        uint32_t                                                                                                                     MinStorageBufferOffsetAlignment() const;
        VkPipelineStageFlags                                                                                                         CopyBuffer(CommandBuffer* const command_buffer, const BufferView& source, const BufferView& destination, VkDeviceSize byte_size, VkDeviceSize src_buffer_offset = 0u, VkDeviceSize dst_buffer_offset = 0u);
        BufferImage                                 CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U, uint32_t mip_level_count = 1U, VkImageCreateFlags image_create_flag_bit = 0, cstring debug_name = nullptr);
        /// @brief Creates a distinct image object backed by an existing image allocation.
        BufferImage                                 CreateAliasingImage(const BufferImage& backing, uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U, uint32_t mip_level_count = 1U, VkImageCreateFlags image_create_flag_bit = 0, cstring debug_name = nullptr);
        VkFormat                                    FindSupportedFormat(Core::Containers::ArrayView<VkFormat> format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
        VkFormat                                    FindDepthFormat();
        VkImageView                                 CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U);
        /// @brief Creates an image view covering the supplied exact subresource range.
        VkImageView                                 CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, const VkImageSubresourceRange& range);
        VkFramebuffer                               CreateFramebuffer(Core::Containers::ArrayView<VkImageView> attachments, const VkRenderPass& render_pass, uint32_t width, uint32_t height, uint32_t layer_number = 1);

        Helpers::Handle<Rendering::Shaders::Shader> CompileShader(Rendering::Specifications::ShaderSpecification& spec);

        /// @brief Queues a shader-file reload request from any producer thread.
        void                                        RequestShaderReload(cstring shader_path);

        /// @brief Reloads queued shaders on the render thread before command recording.
        void                                        FlushShaderReloadRequests();

        /// @brief Reloads a cached shader and invalidates pipelines compiled from its old generation.
        bool                                        ReloadShader(cstring shader_name);

        /// @brief Creates a texture and assigns the optional allocator debug name.
        Rendering::Textures::TextureHandle          CreateTexture(const Rendering::Specifications::TextureSpecification& spec, cstring debug_name = nullptr);
        /// @brief Creates a texture object aliased to the backing allocation of `source`.
        Rendering::Textures::TextureHandle          CreateAliasingTexture(const Rendering::Specifications::TextureSpecification& spec, const Rendering::Textures::TextureHandle& source, cstring debug_name = nullptr);

        /// @brief In-place resize/format change: same handle, same slot, same bindless index.
        /// @return false if handle is not live.
        bool                                        ReconstructTexture(const Rendering::Textures::TextureHandle& handle, const Rendering::Specifications::TextureSpecification& spec);
        /// @brief Reconstructs an aliased texture object against a reconstructed backing texture.
        bool                                        ReconstructAliasingTexture(const Rendering::Textures::TextureHandle& handle, const Rendering::Specifications::TextureSpecification& spec, const Rendering::Textures::TextureHandle& source);

        /// @brief Dirty this handle's bindless descriptor for the next graph frame to refresh.
        void                                        RequestDescriptorUpdate(const Rendering::Textures::TextureHandle& handle);
        void                                        RequestDeferredDescriptorUpdate(const Rendering::Textures::TextureHandle& handle);
        /// @brief Applies queued bindless descriptor writes before render-graph recording.
        /// @details Render-thread only. DeviceSwapchain::Present never writes descriptors.
        void                                        FlushBindlessTextureUpdates();

        /// @brief Timeline-gated disposal. Render-thread only.
        void                                        DestroyTexture(const Rendering::Textures::TextureHandle& handle);

        /// @brief Copies data into the texture's backing image via the ring buffer when it
        ///        fits, else a one-shot staging buffer (returned so the caller can free it
        ///        once the copy's GPU work retires).
        /// @param out_ring_offset When non-null, set to the ring's byte offset if the ring
        ///        path was used (UINT32_MAX otherwise) — the caller must then call
        ///        GpuMem.Ring.Submit(offset, size, signal_value) once it knows the timeline
        ///        value the enqueued copy will signal, or this region is never marked safe
        ///        to reclaim and a later allocation can overwrite it before the GPU reads it.
        /// @param use_staging_ring Set false for independently submitted work whose
        ///        completion is not represented by RenderTimeline.
        BufferView                                  WriteTextureData(CommandBufferPtr command_buf, const Rendering::Textures::TextureHandle& handle, const void* data, uint32_t* out_ring_offset = nullptr, bool use_staging_ring = true);

        Rendering::Renderers::RenderPasses::RenderPass* CreateRenderPass(Rendering::Specifications::RenderPassSpecification spec);

    private:
        friend struct CommandBuffer;

        VulkanLayer                                                       m_layer     = {};
        Core::Containers::UnorderedHashMap<Rendering::QueueType, VkQueue> m_queue_map = {};
        VkDebugUtilsMessengerEXT                                          m_debug_messenger{VK_NULL_HANDLE};
        PFN_vkCreateDebugUtilsMessengerEXT                                __createDebugMessengerPtr{VK_NULL_HANDLE};
        PFN_vkDestroyDebugUtilsMessengerEXT                               __destroyDebugMessengerPtr{VK_NULL_HANDLE};
        PFN_vkCmdBeginDebugUtilsLabelEXT                                  __beginDebugLabelPtr{VK_NULL_HANDLE};
        PFN_vkCmdEndDebugUtilsLabelEXT                                    __endDebugLabelPtr{VK_NULL_HANDLE};
        PFN_vkSetDebugUtilsObjectNameEXT                                  __setDebugObjectNamePtr{VK_NULL_HANDLE};
        PFN_vkCmdBeginRendering                                           __beginRenderingPtr{VK_NULL_HANDLE};
        PFN_vkCmdEndRendering                                             __endRenderingPtr{VK_NULL_HANDLE};
        PFN_vkCmdBeginConditionalRenderingEXT                             __beginConditionalRenderingPtr{VK_NULL_HANDLE};
        PFN_vkCmdEndConditionalRenderingEXT                               __endConditionalRenderingPtr{VK_NULL_HANDLE};
        static VKAPI_ATTR VkBool32 VKAPI_CALL                             __debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    };

    ZDEFINE_PTR(VulkanDevice);
} // namespace ZEngine::Hardwares

namespace ZEngine::Helpers
{
    template <>
    inline void HandleManager<Hardwares::ImageBuffer>::Dispose()
    {
        for (size_t i = 0; i < m_count; ++i)
        {
            m_memory[i].Dispose();
        }
    }
} // namespace ZEngine::Helpers
