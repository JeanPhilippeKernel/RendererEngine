#pragma once
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

/*
 * ^^^^ Headers above are not candidates for sorting by clang-format ^^^^^
 */
#include <Hardwares/VulkanLayer.h>
#include <Rendering/Pools/CommandPool.h>
#include <Rendering/Primitives/Fence.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/ResourceTypes.h>
#include <map>

namespace ZEngine::Windows
{
    class CoreWindow;
}

namespace ZEngine::Hardwares
{

    struct DirtyResource
    {
        uint32_t                              FrameIndex = UINT32_MAX;
        void*                                 Handle     = nullptr;
        void*                                 Data1      = nullptr;
        std::chrono::steady_clock::time_point MarkedAsDirtyTime;
        Rendering::DeviceResourceType         Type;
    };

    struct QueueView
    {
        uint32_t FamilyIndex{0xFFFFFFFF};
        VkQueue  Handle{VK_NULL_HANDLE};
    };

    struct BufferView : public DirtyResource
    {
        VkBuffer      Handle{VK_NULL_HANDLE};
        VmaAllocation Allocation{nullptr};

        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
    };

    struct BufferImage : public DirtyResource
    {
        VkImage       Handle{VK_NULL_HANDLE};
        VkImageView   ViewHandle{VK_NULL_HANDLE};
        VkSampler     Sampler{VK_NULL_HANDLE};
        VmaAllocation Allocation{nullptr};

        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
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

    struct VulkanDevice
    {
        VulkanDevice()                    = delete;
        VulkanDevice(const VulkanDevice&) = delete;
        ~VulkanDevice()                   = delete;

        static void Initialize(const Ref<Windows::CoreWindow>& window);
        static void Deinitialize();
        static void Dispose();

        static void                                    SetCurrentFrameIndex(uint32_t frame);
        static uint32_t                                GetCurrentFrameIndex();
        static VkPhysicalDevice                        GetNativePhysicalDeviceHandle();
        static const VkPhysicalDeviceProperties&       GetPhysicalDeviceProperties();
        static const VkPhysicalDeviceMemoryProperties& GetPhysicalDeviceMemoryProperties();
        static VkDevice                                GetNativeDeviceHandle();
        static VkInstance                              GetNativeInstanceHandle();

        static bool QueueSubmit(
            Rendering::QueueType                    queue_type,
            const VkPipelineStageFlags              wait_stage_flage,
            Rendering::Buffers::CommandBuffer&      command_buffer,
            bool                                    as_instant_submission = false,
            Rendering::Primitives::Semaphore* const wait_semaphore        = nullptr,
            Rendering::Primitives::Semaphore* const signal_semaphore      = nullptr,
            Rendering::Primitives::Fence* const     fence                 = nullptr);

        static void EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const resource_handle);
        static void EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource);

        static void EnqueueBufferForDeletion(BufferView& buffer);
        static void EnqueueBufferImageForDeletion(BufferImage& buffer);

        static bool Present(
            VkSwapchainKHR                          swapchain,
            uint32_t*                               frame_image_index,
            Rendering::Primitives::Semaphore* const wait_semaphore,
            Rendering::Primitives::Semaphore* const render_complete_semaphore,
            Rendering::Primitives::Fence* const     frame_fence);

        static Rendering::Pools::CommandPool* CreateCommandPool(Rendering::QueueType queue_type, uint64_t swapchain_id, bool present_on_swapchain);
        static void                           DisposeCommandPool(const Rendering::Pools::CommandPool* pool);

        static Rendering::Pools::CommandPool* GetCommandPool(Rendering::QueueType queue_type);

        static QueueView GetQueue(Rendering::QueueType type);
        static void      QueueWait(Rendering::QueueType type);
        static void      QueueWaitAll();

        static VkSurfaceKHR       GetSurface();
        static VkSurfaceFormatKHR GetSurfaceFormat();
        static VkPresentModeKHR   GetPresentMode();

        static void        MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data);
        static BufferView  CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags vma_create_flags = 0);
        static void        CopyBuffer(const BufferView& source, const BufferView& destination, VkDeviceSize byte_size);
        static BufferImage CreateImage(
            uint32_t              width,
            uint32_t              height,
            VkImageType           image_type,
            VkImageViewType       image_view_type,
            VkFormat              image_format,
            VkImageTiling         image_tiling,
            VkImageLayout         image_initial_layout,
            VkImageUsageFlags     image_usage,
            VkSharingMode         image_sharing_mode,
            VkSampleCountFlagBits image_sample_count,
            VkMemoryPropertyFlags requested_properties,
            VkImageAspectFlagBits image_aspect_flag,
            uint32_t              layer_count           = 1U,
            VkImageCreateFlags    image_create_flag_bit = 0);

        static VkSampler   CreateImageSampler();
        static VkFormat    FindSupportedFormat(const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
        static VkFormat    FindDepthFormat();
        static VkImageView CreateImageView(
            VkImage               image,
            VkFormat              image_format,
            VkImageViewType       image_view_type,
            VkImageAspectFlagBits image_aspect_flag,
            uint32_t              layer_count = 1U);
        static VkFramebuffer CreateFramebuffer(
            const std::vector<VkImageView>& attachments,
            const VkRenderPass&             render_pass,
            uint32_t                        width,
            uint32_t                        height,
            uint32_t                        layer_number = 1);

        static Ref<Rendering::Buffers::CommandBuffer> BeginInstantCommandBuffer(Rendering::QueueType type);
        static void                                   EndInstantCommandBuffer(Ref<Rendering::Buffers::CommandBuffer>& command);

        static VmaAllocator GetVmaAllocator();

        static std::vector<Ref<Rendering::Pools::CommandPool>> s_command_pool_collection;

    private:
        static std::string                                                                      s_application_name;
        static VulkanLayer                                                                      s_layer;
        static VkInstance                                                                       s_vulkan_instance;
        static VkSurfaceKHR                                                                     s_surface;
        static uint32_t                                                                         s_graphic_family_index;
        static uint32_t                                                                         s_transfer_family_index;
        static uint32_t                                                                         s_current_frame_index;
        static std::map<Rendering::QueueType, VkQueue>                                          s_queue_map;
        static VkDevice                                                                         s_logical_device;
        static VkPhysicalDevice                                                                 s_physical_device;
        static VkPhysicalDeviceProperties                                                       s_physical_device_properties;
        static VkPhysicalDeviceFeatures                                                         s_physical_device_feature;
        static VkPhysicalDeviceMemoryProperties                                                 s_physical_device_memory_properties;
        static VkDebugUtilsMessengerEXT                                                         s_debug_messenger;
        static std::map<uint32_t, std::vector<DirtyResource>>                                   s_deletion_resource_queue;
        static std::deque<DirtyResource>                                                        s_dirty_resource_collection;
        static std::deque<BufferView>                                                           s_dirty_buffer_queue;
        static std::deque<BufferImage>                                                          s_dirty_buffer_image_queue;
        static PFN_vkCreateDebugUtilsMessengerEXT                                               __createDebugMessengerPtr;
        static PFN_vkDestroyDebugUtilsMessengerEXT                                              __destroyDebugMessengerPtr;
        static std::vector<VkSurfaceFormatKHR>                                                  s_surface_format_collection;
        static std::vector<VkPresentModeKHR>                                                    s_present_mode_collection;
        static VkSurfaceFormatKHR                                                               s_surface_format;
        static VkPresentModeKHR                                                                 s_present_mode;
        static VkDescriptorPool                                                                 s_descriptor_pool;
        static VmaAllocator                                                                     s_vma_allocator;
        static std::mutex                                                                       s_frame_value_mutex;
        static std::mutex                                                                       s_queue_mutex;
        static std::mutex                                                                       s_command_pool_mutex;
        static std::mutex                                                                       s_deletion_queue_mutex;
        static std::map<Rendering::QueueType, Ref<Rendering::Pools::CommandPool>>               s_in_device_command_pool_map;
        static std::condition_variable                                                          s_cond;
        static std::atomic_bool                                                                 s_is_executing_instant_command;
        static std::mutex                                                                       s_instant_command_mutex;
        static std::map<Rendering::QueueType, std::map<uint32_t, std::vector<QueueSubmitInfo>>> s_queue_submit_info_pool;
        static void                                                                             __cleanupDirtyResource();
        static void                                                                             __cleanupBufferDirtyResource();
        static void                                                                             __cleanupBufferImageDirtyResource();
        static VKAPI_ATTR VkBool32 VKAPI_CALL                                                   __debugCallback(
                                                              VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                              VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                                              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                              void*                                       pUserData);
    };
} // namespace ZEngine::Hardwares
