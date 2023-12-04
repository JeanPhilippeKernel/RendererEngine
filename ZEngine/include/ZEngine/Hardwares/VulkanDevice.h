#pragma once
#include <map>
#include <vulkan/vulkan.h>
#include <Hardwares/VulkanLayer.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Rendering/Primitives/Fence.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Rendering/ResourceTypes.h>
#include <Rendering/Pools/CommandPool.h>
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

namespace ZEngine::Hardwares
{
    struct QueueView
    {
        uint32_t FamilyIndex{0xFFFFFFFF};
        VkQueue  Handle{VK_NULL_HANDLE};
    };

    struct BufferView
    {
        VkBuffer      Handle{VK_NULL_HANDLE};
        VmaAllocation Allocation{nullptr};

        operator bool() const
        {
            return (Handle != VK_NULL_HANDLE);
        }
    };

    struct BufferImage
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

    struct VulkanDevice
    {
        VulkanDevice()                    = delete;
        VulkanDevice(const VulkanDevice&) = delete;
        ~VulkanDevice()                   = delete;

        static void Initialize(GLFWwindow* const native_window, const std::vector<const char*>& additional_extension_layer_name_collection);
        static void Deinitialize();
        static void Dispose();

        static VkPhysicalDevice                        GetNativePhysicalDeviceHandle();
        static const VkPhysicalDeviceProperties&       GetPhysicalDeviceProperties();
        static const VkPhysicalDeviceMemoryProperties& GetPhysicalDeviceMemoryProperties();
        static VkDevice                                GetNativeDeviceHandle();
        static VkInstance                              GetNativeInstanceHandle();

        static void QueueSubmit(
            Rendering::QueueType                    queue_type,
            const VkPipelineStageFlags              wait_stage_flage,
            VkCommandBuffer const                   buffer_handle,
            Rendering::Primitives::Semaphore* const wait_semaphore,
            Rendering::Primitives::Semaphore* const signal_semaphore,
            Rendering::Primitives::Fence* const     fence);

        static void EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const resource_handle);

        static void EnqueueBufferForDeletion(BufferView& buffer);
        static void EnqueueBufferImageForDeletion(BufferImage& buffer);

        static void CleanupResource();
        static bool HasPendingCleanupResource();
        static void Present(VkSwapchainKHR swapchain, uint32_t* frame_image_index, const std::vector<Rendering::Primitives::Semaphore*>& wait_semaphore_collection);

        static Rendering::Pools::CommandPool*                  CreateCommandPool(Rendering::QueueType queue_type, uint64_t swapchain_id, bool present_on_swapchain);
        static std::vector<Ref<Rendering::Pools::CommandPool>> GetAllCommandPools(uint64_t swapchain_id = 0);
        static void                                            DisposeCommandPool(const Rendering::Pools::CommandPool* pool);

        static QueueView GetQueue(Rendering::QueueType type);
        static void      QueueWait(Rendering::QueueType type);
        static void      QueueWaitAll();

        static VkSurfaceKHR       GetSurface();
        static VkSurfaceFormatKHR GetSurfaceFormat();
        static VkPresentModeKHR   GetPresentMode();

        static void       MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data);
        static BufferView CreateBuffer(
            VkDeviceSize             byte_size,
            VkBufferUsageFlags       buffer_usage,
            VmaAllocationCreateFlags vma_create_flags = 0);
        static void        CopyBuffer(const BufferView& source, const BufferView& destination, VkDeviceSize byte_size);
        static BufferImage CreateImage(
            uint32_t              width,
            uint32_t              height,
            VkImageType           image_type,
            VkFormat              image_format,
            VkImageTiling         image_tiling,
            VkImageLayout         image_initial_layout,
            VkImageUsageFlags     image_usage,
            VkSharingMode         image_sharing_mode,
            VkSampleCountFlagBits image_sample_count,
            VkMemoryPropertyFlags requested_properties,
            VkImageAspectFlagBits image_aspect_flag);

        static VkSampler   CreateImageSampler();
        static VkFormat    FindSupportedFormat(const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags);
        static VkFormat    FindDepthFormat();
        static VkImageView CreateImageView(VkImage image, VkFormat image_format, VkImageAspectFlagBits image_aspect_flag);
        static void        CopyBufferToImage(
                   const Rendering::QueueType&                                   queue_type,
                   const BufferView&                                             source,
                   BufferImage&                                                  destination,
                   uint32_t                                                      width,
                   uint32_t                                                      height,
                   uint32_t                                                      start_copy_after_barrier_index = 0,
                   const std::vector<Rendering::Primitives::ImageMemoryBarrier>& memory_barriers                = {});
        static VkFramebuffer CreateFramebuffer(
            const std::vector<VkImageView>& attachments,
            const VkRenderPass&             render_pass,
            uint32_t                        width,
            uint32_t                        height,
            uint32_t                        layer_number = 1);

        static Rendering::Buffers::CommandBuffer* BeginInstantCommandBuffer(Rendering::QueueType type);
        static void                               EndInstantCommandBuffer(Rendering::Buffers::CommandBuffer* const command);

        static VmaAllocator GetVmaAllocator();

    private:
        static std::string                                     m_application_name;
        static VulkanLayer                                     m_layer;
        static VkInstance                                      m_vulkan_instance;
        static VkSurfaceKHR                                    m_surface;
        static uint32_t                                        m_graphic_family_index;
        static uint32_t                                        m_transfer_family_index;
        static std::map<Rendering::QueueType, VkQueue>         m_queue_map;
        static VkDevice                                        m_logical_device;
        static VkPhysicalDevice                                m_physical_device;
        static VkPhysicalDeviceProperties                      m_physical_device_properties;
        static VkPhysicalDeviceFeatures                        m_physical_device_feature;
        static VkPhysicalDeviceMemoryProperties                m_physical_device_memory_properties;
        static VkDebugUtilsMessengerEXT                        m_debug_messenger;
        static std::map<uint32_t, std::vector<void*>>          m_deletion_resource_queue;
        static std::vector<BufferView>                         s_dirty_buffer_queue;
        static std::vector<BufferImage>                        s_dirty_buffer_image_queue;
        static std::vector<Ref<Rendering::Pools::CommandPool>> m_command_pool_collection;
        static PFN_vkCreateDebugUtilsMessengerEXT              __createDebugMessengerPtr;
        static PFN_vkDestroyDebugUtilsMessengerEXT             __destroyDebugMessengerPtr;
        static std::vector<VkSurfaceFormatKHR>                 m_surface_format_collection;
        static std::vector<VkPresentModeKHR>                   m_present_mode_collection;
        static VkSurfaceFormatKHR                              m_surface_format;
        static VkPresentModeKHR                                m_present_mode;
        static VkDescriptorPool                                m_descriptor_pool;
        static VmaAllocator                                    s_vma_allocator;
        static VKAPI_ATTR VkBool32 VKAPI_CALL                  __debugCallback(
                             VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                             VkDebugUtilsMessageTypeFlagsEXT             messageType,
                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                             void*                                       pUserData);

    private:
        static std::mutex                                                         m_queue_mutex;
        static std::mutex                                                         m_command_pool_mutex;
        static std::mutex                                                         m_deletion_queue_mutex;
        static std::map<Rendering::QueueType, Ref<Rendering::Pools::CommandPool>> m_in_device_command_pool_map;
        static std::condition_variable                                            m_cond;
        static std::atomic_bool                                                   m_is_executing_instant_command;
        static std::mutex                                                         m_instant_command_mutex;
    };
} // namespace ZEngine::Hardwares
