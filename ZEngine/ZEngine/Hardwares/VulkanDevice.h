#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

/*
 * ^^^^ Headers above are not candidates for sorting by clang-format ^^^^^
 */
#include <Hardwares/VulkanLayer.h>
#include <Helpers/HandleManager.h>
#include <Primitives/Fence.h>
#include <Primitives/Semaphore.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <Rendering/Pools/CommandPool.h>
#include <Rendering/ResourceTypes.h>
#include <Rendering/Specifications/AttachmentSpecification.h>
#include <Rendering/Specifications/RenderPassSpecification.h>
#include <Rendering/Specifications/ShaderSpecification.h>
#include <map>

namespace ZEngine::Windows
{
    class CoreWindow;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct RenderPass;
    struct Attachment;
} // namespace ZEngine::Rendering::Renderers::RenderPasses

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct GraphicPipeline;
}

namespace ZEngine::Rendering::Shaders
{
    class Shader;
}

namespace ZEngine::Hardwares
{

    struct DirtyResource
    {
        uint32_t                      FrameIndex = UINT32_MAX;
        void*                         Handle     = nullptr;
        void*                         Data1      = nullptr;
        Rendering::DeviceResourceType Type;
    };

    struct QueueView
    {
        uint32_t FamilyIndex{0xFFFFFFFF};
        VkQueue  Handle{VK_NULL_HANDLE};
    };

    struct QueueSubmitInfo
    {
        bool                               IsImmediate{false};
        uint32_t                           DestinationStageMask;
        Rendering::QueueType               Type;
        Rendering::Buffers::CommandBuffer& Buffer;
    };

    struct QueueSubmission
    {
        Rendering::Primitives::Semaphore* SignalSemaphore{nullptr};
        Rendering::Primitives::Fence*     SignalFence{nullptr};
        VkSubmitInfo                      Submit;
    };

    struct WriteDescriptorSetRequest;
    struct CommandBufferManager;
    struct VulkanDevice;

    struct CommandBufferManager
    {
        void                                                         Initialize(VulkanDevice* device, uint8_t swapchain_image_count = 3, int thread_count = 1);
        void                                                         Deinitialize();
        Rendering::Buffers::CommandBuffer*                           GetCommandBuffer(uint8_t frame_index, bool begin = true);
        Rendering::Buffers::CommandBuffer*                           GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, bool begin = true);
        void                                                         EndInstantCommandBuffer(Rendering::Buffers::CommandBuffer* const buffer, VulkanDevice* const device, int wait_flag = 0);
        Rendering::Pools::CommandPool*                               GetCommandPool(Rendering::QueueType type, uint8_t frame_index);
        int                                                          GetPoolFromIndex(Rendering::QueueType type, uint8_t index);
        void                                                         ResetPool(int frame_index);

        VulkanDevice*                                                Device                  = nullptr;
        const int                                                    MaxBufferPerPool        = 4;
        std::vector<Helpers::Ref<Rendering::Pools::CommandPool>>     CommandPools            = {};
        std::vector<Helpers::Ref<Rendering::Pools::CommandPool>>     TransferCommandPools    = {};
        std::vector<Helpers::Ref<Rendering::Buffers::CommandBuffer>> CommandBuffers          = {};
        std::vector<Helpers::Ref<Rendering::Buffers::CommandBuffer>> TransferCommandBuffers  = {};
        int                                                          TotalCommandBufferCount = 0;

    private:
        int                                            m_total_pool_count{0};
        std::condition_variable                        m_cond;
        std::atomic_bool                               m_executing_instant_command{false};
        std::mutex                                     m_instant_command_mutex;
        Helpers::Ref<Rendering::Primitives::Semaphore> m_instant_semaphore;
        Helpers::Ref<Rendering::Primitives::Fence>     m_instant_fence;
    };

    struct WriteDescriptorSetRequest
    {
        int              Handle;
        uint32_t         FrameIndex;
        VkDescriptorSet  DstSet;
        uint32_t         Binding;
        uint32_t         DstArrayElement;
        uint32_t         DescriptorCount;
        VkDescriptorType DescriptorType;
    };

    struct VulkanDevice
    {
        const std::string_view                                       ApplicationName                   = "Tetragrama";
        const std::string_view                                       EngineName                        = "ZEngine";
        bool                                                         HasSeperateTransfertQueueFamily   = false;
        uint32_t                                                     SwapchainImageIndex               = std::numeric_limits<uint8_t>::max();
        uint32_t                                                     CurrentFrameIndex                 = std::numeric_limits<uint8_t>::max();
        uint32_t                                                     PreviousFrameIndex                = std::numeric_limits<uint8_t>::max();
        uint32_t                                                     SwapchainImageCount               = 3;
        uint32_t                                                     SwapchainImageWidth               = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     SwapchainImageHeight              = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     GraphicFamilyIndex                = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     TransferFamilyIndex               = std::numeric_limits<uint32_t>::max();
        uint32_t                                                     EnqueuedCommandbufferIndex        = 0;
        VkInstance                                                   Instance                          = VK_NULL_HANDLE;
        VkSurfaceKHR                                                 Surface                           = VK_NULL_HANDLE;
        VkSurfaceFormatKHR                                           SurfaceFormat                     = {};
        VkPresentModeKHR                                             PresentMode                       = {};
        VkPhysicalDeviceProperties                                   PhysicalDeviceProperties          = {};
        VkDevice                                                     LogicalDevice                     = VK_NULL_HANDLE;
        VkPhysicalDevice                                             PhysicalDevice                    = VK_NULL_HANDLE;
        VkPhysicalDeviceFeatures                                     PhysicalDeviceFeature             = {};
        VkPhysicalDeviceMemoryProperties                             PhysicalDeviceMemoryProperties    = {};
        VkSwapchainKHR                                               SwapchainHandle                   = VK_NULL_HANDLE;
        VmaAllocator                                                 VmaAllocator                      = nullptr;
        Helpers::Ref<Rendering::Renderers::RenderPasses::Attachment> SwapchainAttachment               = {};
        std::vector<VkImageView>                                     SwapchainImageViews               = {};
        std::vector<VkFramebuffer>                                   SwapchainFramebuffers             = {};
        std::vector<Helpers::Ref<Rendering::Primitives::Semaphore>>  SwapchainAcquiredSemaphores       = {};
        std::vector<Helpers::Ref<Rendering::Primitives::Semaphore>>  SwapchainRenderCompleteSemaphores = {};
        std::vector<Helpers::Ref<Rendering::Primitives::Fence>>      SwapchainSignalFences             = {};
        std::vector<Rendering::Buffers::CommandBuffer*>              EnqueuedCommandbuffers            = {};
        Helpers::Ref<Rendering::Textures::TextureHandleManager>      GlobalTextures                    = Helpers::CreateRef<Rendering::Textures::TextureHandleManager>(600);
        std::atomic_bool                                             RunningDirtyCollector             = true;
        std::atomic_uint                                             IdleFrameCount                    = 0;
        std::atomic_uint                                             IdleFrameThreshold                = SwapchainImageCount * 3;
        std::condition_variable                                      DirtyCollectorCond                = {};
        std::mutex                                                   DirtyMutex                        = {};
        Windows::CoreWindow*                                         CurrentWindow                     = nullptr;

        void                                                         Initialize(const Helpers::Ref<Windows::CoreWindow>& window);
        void                                                         Deinitialize();
        void                                                         Dispose();
        bool                                                         QueueSubmit(const VkPipelineStageFlags wait_stage_flag, Rendering::Buffers::CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore = nullptr, Rendering::Primitives::Fence* const fence = nullptr);
        void                                                         EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const resource_handle);
        void                                                         EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource);
        void                                                         EnqueueBufferForDeletion(Rendering::Buffers::BufferView& buffer);
        void                                                         EnqueueBufferImageForDeletion(Rendering::Buffers::BufferImage& buffer);
        QueueView                                                    GetQueue(Rendering::QueueType type);
        void                                                         QueueWait(Rendering::QueueType type);
        void                                                         QueueWaitAll();
        void                                                         MapAndCopyToMemory(Rendering::Buffers::BufferView& buffer, size_t data_size, const void* data);
        Rendering::Buffers::BufferView                               CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags vma_create_flags = 0);
        void                                                         CopyBuffer(const Rendering::Buffers::BufferView& source, const Rendering::Buffers::BufferView& destination, VkDeviceSize byte_size);
        Rendering::Buffers::BufferImage                              CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U, VkImageCreateFlags image_create_flag_bit = 0);
        VkSampler                                                    CreateImageSampler();
        VkFormat                                                     FindSupportedFormat(const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
        VkFormat                                                     FindDepthFormat();
        VkImageView                                                  CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count = 1U);
        VkFramebuffer                                                CreateFramebuffer(const std::vector<VkImageView>& attachments, const VkRenderPass& render_pass, uint32_t width, uint32_t height, uint32_t layer_number = 1);
        void                                                         CreateSwapchain();
        void                                                         ResizeSwapchain();
        void                                                         DisposeSwapchain();
        void                                                         NewFrame();
        void                                                         Present();
        void                                                         IncrementFrameImageCount();
        Rendering::Buffers::CommandBuffer*                           GetCommandBuffer(bool begin = true);
        Rendering::Buffers::CommandBuffer*                           GetInstantCommandBuffer(Rendering::QueueType type, bool begin = true);
        void                                                         EnqueueInstantCommandBuffer(Rendering::Buffers::CommandBuffer* const buffer, int wait_flag = 0);
        void                                                         EnqueueCommandBuffer(Rendering::Buffers::CommandBuffer* const buffer);

        void                                                         DirtyCollector();

    private:
        VulkanLayer                                             m_layer{};
        CommandBufferManager                                    m_buffer_manager{};
        std::map<Rendering::QueueType, VkQueue>                 m_queue_map{};
        Helpers::HandleManager<DirtyResource>                   m_dirty_resources{300};
        Helpers::HandleManager<Rendering::Buffers::BufferView>  m_dirty_buffers{500};
        Helpers::HandleManager<Rendering::Buffers::BufferImage> m_dirty_buffer_images{500};
        VkDebugUtilsMessengerEXT                                m_debug_messenger{VK_NULL_HANDLE};
        PFN_vkCreateDebugUtilsMessengerEXT                      __createDebugMessengerPtr{VK_NULL_HANDLE};
        PFN_vkDestroyDebugUtilsMessengerEXT                     __destroyDebugMessengerPtr{VK_NULL_HANDLE};
        void                                                    __cleanupDirtyResource();
        void                                                    __cleanupBufferDirtyResource();
        void                                                    __cleanupBufferImageDirtyResource();
        static VKAPI_ATTR VkBool32 VKAPI_CALL                   __debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    };
} // namespace ZEngine::Hardwares
