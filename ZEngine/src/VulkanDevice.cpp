#include <pch.h>
#include <ZEngineDef.h>
#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>

using namespace ZEngine::Rendering::Primitives;

namespace ZEngine::Hardwares
{
    std::string                                                                  VulkanDevice::m_application_name                  = "ZEngine";
    VulkanLayer                                                                  VulkanDevice::m_layer                             = {};
    VkInstance                                                                   VulkanDevice::m_vulkan_instance                   = VK_NULL_HANDLE;
    VkSurfaceKHR                                                                 VulkanDevice::m_surface                           = VK_NULL_HANDLE;
    uint32_t                                                                     VulkanDevice::m_graphic_family_index              = 0;
    uint32_t                                                                     VulkanDevice::m_transfer_family_index             = 0;
    std::unordered_map<Rendering::QueueType, VkQueue>                            VulkanDevice::m_queue_map                         = {};
    VkDevice                                                                     VulkanDevice::m_logical_device                    = VK_NULL_HANDLE;
    VkPhysicalDevice                                                             VulkanDevice::m_physical_device                   = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties                                                   VulkanDevice::m_physical_device_properties        = {};
    VkPhysicalDeviceFeatures                                                     VulkanDevice::m_physical_device_feature           = {};
    VkPhysicalDeviceMemoryProperties                                             VulkanDevice::m_physical_device_memory_properties = {};
    VkDebugUtilsMessengerEXT                                                     VulkanDevice::m_debug_messenger                   = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, std::vector<void*>>                             VulkanDevice::m_deletion_resource_queue           = {};
    std::vector<Ref<Rendering::Pools::CommandPool>>                              VulkanDevice::m_command_pool_collection           = {};
    PFN_vkCreateDebugUtilsMessengerEXT                                           VulkanDevice::__createDebugMessengerPtr           = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                                          VulkanDevice::__destroyDebugMessengerPtr          = nullptr;
    std::vector<VkSurfaceFormatKHR>                                              VulkanDevice::m_surface_format_collection         = {};
    std::vector<VkPresentModeKHR>                                                VulkanDevice::m_present_mode_collection           = {};
    VkSurfaceFormatKHR                                                           VulkanDevice::m_surface_format                    = {};
    VkPresentModeKHR                                                             VulkanDevice::m_present_mode                      = {};
    VkDescriptorPool                                                             VulkanDevice::m_descriptor_pool                   = VK_NULL_HANDLE;
    std::unordered_map<Rendering::QueueType, Ref<Rendering::Pools::CommandPool>> VulkanDevice::m_in_device_command_pool_map;
    std::mutex                                                                   VulkanDevice::m_queue_mutex;
    std::mutex                                                                   VulkanDevice::m_command_pool_mutex;
    std::mutex                                                                   VulkanDevice::m_deletion_queue_mutex;
    std::condition_variable                                                      VulkanDevice::m_cond;
    std::atomic_bool                                                             VulkanDevice::m_is_executing_instant_command = false;
    std::mutex                                                                   VulkanDevice::m_instant_command_mutex;

    void VulkanDevice::Initialize(GLFWwindow* const native_window, const std::vector<const char*>& additional_extension_layer_name_collection)
    {
        /*Create Vulkan Instance*/
        VkApplicationInfo app_info                = {};
        app_info.sType                            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName                 = m_application_name.data();
        app_info.pEngineName                      = m_application_name.data();
        app_info.apiVersion                       = VK_API_VERSION_1_3;
        app_info.engineVersion                    = 1;
        app_info.applicationVersion               = 1;
        app_info.pNext                            = nullptr;
        VkInstanceCreateInfo instance_create_info = {};
        instance_create_info.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo     = &app_info;
        instance_create_info.pNext                = nullptr;
        instance_create_info.flags                = 0;

        auto layer_properties = m_layer.GetInstanceLayerProperties();

        std::vector<const char*>   enabled_layer_name_collection;
        std::vector<LayerProperty> selected_layer_property_collection;

#ifdef ENABLE_VULKAN_VALIDATION_LAYER
        std::unordered_set<std::string> validation_layer_name_collection = {
            "VK_LAYER_LUNARG_api_dump", "VK_LAYER_KHRONOS_validation", "VK_LAYER_LUNARG_monitor", "VK_LAYER_LUNARG_screenshot"};

        for (std::string_view layer_name : validation_layer_name_collection)
        {
            auto find_it = std::find_if(std::begin(layer_properties), std::end(layer_properties), [&](const LayerProperty& layer_property) {
                return std::string_view(layer_property.Properties.layerName) == layer_name;
            });
            if (find_it == std::end(layer_properties))
            {
                continue;
            }

            enabled_layer_name_collection.push_back(find_it->Properties.layerName);
            selected_layer_property_collection.push_back(*find_it);
        }
#endif

        std::vector<const char*> enabled_extension_layer_name_collection;

        for (const LayerProperty& layer : selected_layer_property_collection)
        {
            for (const auto& extension : layer.ExtensionCollection)
            {
                enabled_extension_layer_name_collection.push_back(extension.extensionName);
            }
        }

        if (!additional_extension_layer_name_collection.empty())
        {
            for (const auto& extension : additional_extension_layer_name_collection)
            {
                enabled_extension_layer_name_collection.push_back(extension);
            }
        }

        instance_create_info.enabledLayerCount       = enabled_layer_name_collection.size();
        instance_create_info.ppEnabledLayerNames     = enabled_layer_name_collection.data();
        instance_create_info.enabledExtensionCount   = enabled_extension_layer_name_collection.size();
        instance_create_info.ppEnabledExtensionNames = enabled_extension_layer_name_collection.data();

        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &m_vulkan_instance);

        if (result == VK_ERROR_INCOMPATIBLE_DRIVER)
        {
            ZENGINE_CORE_CRITICAL("Failed to create Vulkan Instance. Incompatible driver")
            ZENGINE_EXIT_FAILURE()
        }

        if (result == VK_INCOMPLETE)
        {
            ZENGINE_CORE_CRITICAL("Failed to create Vulkan Instance. Confugration incomplete!")
            ZENGINE_EXIT_FAILURE()
        }

        /*Create Message Callback*/
        VkDebugUtilsMessengerCreateInfoEXT messenger_create_info = {};
        messenger_create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_create_info.messageSeverity                    = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        messenger_create_info.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        messenger_create_info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger_create_info.pfnUserCallback = __debugCallback;
        messenger_create_info.pUserData       = nullptr; // Optional

        __createDebugMessengerPtr  = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_vulkan_instance, "vkCreateDebugUtilsMessengerEXT"));
        __destroyDebugMessengerPtr = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_vulkan_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (__createDebugMessengerPtr)
        {
            __createDebugMessengerPtr(m_vulkan_instance, &messenger_create_info, nullptr, &m_debug_messenger);
        }

        ZENGINE_VALIDATE_ASSERT(glfwCreateWindowSurface(m_vulkan_instance, native_window, nullptr, &m_surface) == VK_SUCCESS, "Failed Window Surface from GLFW");

        /*Create Vulkan Device*/
        ZENGINE_VALIDATE_ASSERT(m_vulkan_instance != VK_NULL_HANDLE, "A Vulkan Instance must be created first!")

        uint32_t gpu_device_count{0};
        vkEnumeratePhysicalDevices(m_vulkan_instance, &gpu_device_count, nullptr);

        std::vector<VkPhysicalDevice> physical_device_collection(gpu_device_count);
        vkEnumeratePhysicalDevices(m_vulkan_instance, &gpu_device_count, physical_device_collection.data());

        for (VkPhysicalDevice physical_device : physical_device_collection)
        {
            VkPhysicalDeviceProperties physical_device_properties;
            VkPhysicalDeviceFeatures   physical_device_feature;
            vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
            vkGetPhysicalDeviceFeatures(physical_device, &physical_device_feature);

            if ((physical_device_feature.geometryShader == VK_TRUE) && (physical_device_feature.samplerAnisotropy == VK_TRUE) &&
                (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
            {
                m_physical_device            = physical_device;
                m_physical_device_properties = physical_device_properties;
                m_physical_device_feature    = physical_device_feature;
                vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_physical_device_memory_properties);
                break;
            }
        }

        std::vector<const char*> requested_device_enabled_layer_name_collection   = {};
        std::vector<const char*> requested_device_extension_layer_name_collection = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        for (LayerProperty& layer : selected_layer_property_collection)
        {
            m_layer.GetExtensionProperties(layer, &m_physical_device);

            if (!layer.DeviceExtensionCollection.empty())
            {
                requested_device_enabled_layer_name_collection.push_back(layer.Properties.layerName);
                for (const auto& extension_property : layer.DeviceExtensionCollection)
                {
                    requested_device_extension_layer_name_collection.push_back(extension_property.extensionName);
                }
            }
        }

        uint32_t physical_device_queue_family_count{0};
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &physical_device_queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> physical_device_queue_family_collection;
        physical_device_queue_family_collection.resize(physical_device_queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &physical_device_queue_family_count, physical_device_queue_family_collection.data());

        uint32_t                queue_family_index      = 0;
        VkQueueFamilyProperties queue_family_properties = {};
        for (size_t index = 0; index < physical_device_queue_family_count; ++index)
        {
            if (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                // Ensuring presentation support
                if (m_surface)
                {
                    VkBool32 present_support = false;

                    ZENGINE_VALIDATE_ASSERT(
                        vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, index, m_surface, &present_support) == VK_SUCCESS,
                        "Failed to get device surface support information")

                    if (present_support)
                    {
                        m_graphic_family_index = index;
                    }
                }
            }
            else if (
                (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
                (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
            {
                m_transfer_family_index = index;
            }
        }

        float                                queue_prorities              = 1.0f;
        auto                                 family_index_collection      = std::array{m_graphic_family_index, m_transfer_family_index};
        std::vector<VkDeviceQueueCreateInfo> queue_create_info_collection = {};
        for (uint32_t queue_family_index : family_index_collection)
        {
            VkDeviceQueueCreateInfo queue_create_info = {};
            queue_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.pQueuePriorities        = &queue_prorities;
            queue_create_info.queueFamilyIndex        = queue_family_index;
            queue_create_info.queueCount              = 1;
            queue_create_info.pNext                   = nullptr;
            queue_create_info_collection.emplace_back(queue_create_info);
        }

        VkDeviceCreateInfo device_create_info    = {};
        device_create_info.sType                 = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount  = queue_create_info_collection.size();
        device_create_info.pQueueCreateInfos     = queue_create_info_collection.data();
        device_create_info.enabledExtensionCount = static_cast<uint32_t>(requested_device_extension_layer_name_collection.size());
        device_create_info.ppEnabledExtensionNames =
            (requested_device_extension_layer_name_collection.size() > 0) ? requested_device_extension_layer_name_collection.data() : nullptr;
        device_create_info.enabledLayerCount   = static_cast<uint32_t>(requested_device_enabled_layer_name_collection.size());
        device_create_info.ppEnabledLayerNames = (requested_device_enabled_layer_name_collection.size() > 0) ? requested_device_enabled_layer_name_collection.data() : nullptr;
        device_create_info.pEnabledFeatures    = nullptr;
        device_create_info.pNext               = nullptr;

        ZENGINE_VALIDATE_ASSERT(vkCreateDevice(m_physical_device, &device_create_info, nullptr, &m_logical_device) == VK_SUCCESS, "Failed to create GPU logical device")

        /*Create Vulkan Graphic Queue*/
        m_queue_map[Rendering::QueueType::GRAPHIC_QUEUE] = VK_NULL_HANDLE;
        vkGetDeviceQueue(m_logical_device, m_graphic_family_index, 0, &(m_queue_map[Rendering::QueueType::GRAPHIC_QUEUE]));

        /*Create Vulkan Transfer Queue*/
        m_queue_map[Rendering::QueueType::TRANSFER_QUEUE] = VK_NULL_HANDLE;
        vkGetDeviceQueue(m_logical_device, m_transfer_family_index, 0, &(m_queue_map[Rendering::QueueType::TRANSFER_QUEUE]));

        /**/
        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, nullptr);
        if (format_count != 0)
        {
            m_surface_format_collection.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, m_surface_format_collection.data());
        }

        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &present_mode_count, nullptr);
        if (present_mode_count != 0)
        {
            m_present_mode_collection.resize(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &present_mode_count, m_present_mode_collection.data());
        }

        m_surface_format = (m_surface_format_collection.size() > 0) ? m_surface_format_collection[0] : VkSurfaceFormatKHR{};
        for (const VkSurfaceFormatKHR& format_khr : m_surface_format_collection)
        {
            // default is: VK_FORMAT_B8G8R8A8_SRGB
            // but Imgui wants : VK_FORMAT_B8G8R8A8_UNORM ...
            if ((format_khr.format == VK_FORMAT_B8G8R8A8_UNORM) && (format_khr.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
            {
                m_surface_format = format_khr;
                break;
            }
        }

        m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (const VkPresentModeKHR present_mode_khr : m_present_mode_collection)
        {
            if (present_mode_khr == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                m_present_mode = present_mode_khr;
                break;
            }
        }

        /*Create DescriptorPool*/
        std::vector<VkDescriptorPoolSize> pool_sizes = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 10},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 10},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 10},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 10}};

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets                    = 10 * pool_sizes.size();
        pool_info.poolSizeCount              = pool_sizes.size();
        pool_info.pPoolSizes                 = pool_sizes.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(m_logical_device, &pool_info, nullptr, &m_descriptor_pool) == VK_SUCCESS, "Failed to create DescriptorPool -- ImGuiLayer")

        /*In Device Command Pool*/
        m_in_device_command_pool_map[Rendering::QueueType::GRAPHIC_QUEUE]  = CreateRef<Rendering::Pools::CommandPool>(Rendering::QueueType::GRAPHIC_QUEUE, false);
        m_in_device_command_pool_map[Rendering::QueueType::TRANSFER_QUEUE] = CreateRef<Rendering::Pools::CommandPool>(Rendering::QueueType::TRANSFER_QUEUE, false);
    }

    void VulkanDevice::Deinitialize()
    {
        QueueWaitAll();

        m_command_pool_collection.clear();
        m_command_pool_collection.shrink_to_fit();
        m_in_device_command_pool_map.clear();

        CleanupResource();

        ZENGINE_DESTROY_VULKAN_HANDLE(m_logical_device, vkDestroyDescriptorPool, m_descriptor_pool, nullptr)
        ZENGINE_DESTROY_VULKAN_HANDLE(m_vulkan_instance, vkDestroySurfaceKHR, m_surface, nullptr)
    }

    void VulkanDevice::Dispose()
    {
        vkDestroyDevice(m_logical_device, nullptr);
        if (__destroyDebugMessengerPtr)
        {
            ZENGINE_DESTROY_VULKAN_HANDLE(m_vulkan_instance, __destroyDebugMessengerPtr, m_debug_messenger, nullptr)
        }
        vkDestroyInstance(m_vulkan_instance, nullptr);

        m_logical_device           = VK_NULL_HANDLE;
        m_vulkan_instance          = VK_NULL_HANDLE;
        __destroyDebugMessengerPtr = nullptr;
        __createDebugMessengerPtr  = nullptr;
    }

    VkDevice VulkanDevice::GetNativeDeviceHandle()
    {
        return m_logical_device;
    }

    VkInstance VulkanDevice::GetNativeInstanceHandle()
    {
        return m_vulkan_instance;
    }

    void VulkanDevice::QueueSubmit(
        Rendering::QueueType                    queue_type,
        const VkPipelineStageFlags              wait_stage_flage,
        VkCommandBuffer const                   buffer_handle,
        Rendering::Primitives::Semaphore* const wait_semaphore,
        Rendering::Primitives::Semaphore* const signal_semaphore,
        Rendering::Primitives::Fence* const     signal_fence)
    {
        std::lock_guard lock(m_queue_mutex);

        if (!buffer_handle)
        {
            ZENGINE_CORE_ERROR("Command Buffer handle is null, Failed to submit to Queue")
            return;
        }

        if (wait_semaphore)
        {
            ZENGINE_VALIDATE_ASSERT(wait_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Idle, "Wait semaphore is in an idle state and will never be signaled")
        }

        if (signal_semaphore)
        {
            ZENGINE_VALIDATE_ASSERT(signal_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")
        }

        if (signal_fence)
        {
            ZENGINE_VALIDATE_ASSERT(signal_fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")
        }

        std::array<VkSemaphore, 1> wait_semaphore_collection   = {wait_semaphore ? wait_semaphore->GetHandle() : nullptr};
        std::array<VkSemaphore, 1> signal_semaphore_collection = {signal_semaphore ? signal_semaphore->GetHandle() : nullptr};

        VkSubmitInfo submit_info         = {};
        submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext                = nullptr;
        submit_info.waitSemaphoreCount   = wait_semaphore != nullptr ? 1 : 0;
        submit_info.pWaitSemaphores      = wait_semaphore != nullptr ? wait_semaphore_collection.data() : nullptr;
        submit_info.signalSemaphoreCount = signal_semaphore != nullptr ? 1 : 0;
        submit_info.pSignalSemaphores    = signal_semaphore != nullptr ? signal_semaphore_collection.data() : nullptr;
        submit_info.pWaitDstStageMask    = &wait_stage_flage;
        submit_info.commandBufferCount   = 1;
        submit_info.pCommandBuffers      = &buffer_handle;

        auto fence_ptr = signal_fence ? signal_fence->GetHandle() : nullptr;
        ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(GetQueue(queue_type).Handle, 1, &submit_info, fence_ptr) == VK_SUCCESS, "Failed to submit queue")

        if (wait_semaphore)
        {
            wait_semaphore->SetState(SemaphoreState::Idle);
        }

        if (signal_semaphore)
        {
            signal_semaphore->SetState(SemaphoreState::Submitted);
        }

        if (signal_fence)
        {
            signal_fence->SetState(FenceState::Submitted);
        }
    }

    void VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const resource_handle)
    {
        std::lock_guard lock(m_deletion_queue_mutex);
        if (resource_handle)
        {
            m_deletion_resource_queue[static_cast<uint32_t>(resource_type)].emplace_back(resource_handle);
        }
    }

    void VulkanDevice::CleanupResource()
    {
        std::lock_guard lock(m_deletion_queue_mutex);

        for (auto resource : m_deletion_resource_queue)
        {
            Rendering::DeviceResourceType resource_type = static_cast<Rendering::DeviceResourceType>(resource.first);
            for (void* handle : resource.second)
            {
                switch (resource_type)
                {
                    case Rendering::DeviceResourceType::SAMPLER:
                        vkDestroySampler(m_logical_device, reinterpret_cast<VkSampler>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::FRAMEBUFFER:
                        vkDestroyFramebuffer(m_logical_device, reinterpret_cast<VkFramebuffer>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::IMAGEVIEW:
                        vkDestroyImageView(m_logical_device, reinterpret_cast<VkImageView>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::IMAGE:
                        vkDestroyImage(m_logical_device, reinterpret_cast<VkImage>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::RENDERPASS:
                        vkDestroyRenderPass(m_logical_device, reinterpret_cast<VkRenderPass>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::BUFFERMEMORY:
                        vkFreeMemory(m_logical_device, reinterpret_cast<VkDeviceMemory>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::BUFFER:
                        vkDestroyBuffer(m_logical_device, reinterpret_cast<VkBuffer>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::PIPELINE_LAYOUT:
                        vkDestroyPipelineLayout(m_logical_device, reinterpret_cast<VkPipelineLayout>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::PIPELINE:
                        vkDestroyPipeline(m_logical_device, reinterpret_cast<VkPipeline>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT:
                        vkDestroyDescriptorSetLayout(m_logical_device, reinterpret_cast<VkDescriptorSetLayout>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::SEMAPHORE:
                        vkDestroySemaphore(m_logical_device, reinterpret_cast<VkSemaphore>(handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::FENCE:
                        vkDestroyFence(m_logical_device, reinterpret_cast<VkFence>(handle), nullptr);
                        break;
                }
            }
        }

        m_deletion_resource_queue.clear();
    }

    bool VulkanDevice::HasPendingCleanupResource()
    {
        std::lock_guard lock(m_deletion_queue_mutex);
        return m_deletion_resource_queue.size() >= static_cast<uint32_t>(Rendering::DeviceResourceType::RESOURCE_COUNT);
    }

    void VulkanDevice::Present(VkSwapchainKHR swapchain, uint32_t* frame_image_index, const std::vector<Rendering::Primitives::Semaphore*>& wait_semaphore_collection)
    {
        std::vector<VkSemaphore> wait_semaphore_handle_collection(wait_semaphore_collection.size(), VK_NULL_HANDLE);
        for (uint32_t i = 0; i < wait_semaphore_handle_collection.size(); ++i)
        {
            wait_semaphore_handle_collection[i] = wait_semaphore_collection[i]->GetHandle();
        }

        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = wait_semaphore_handle_collection.size();
        present_info.pWaitSemaphores    = wait_semaphore_handle_collection.data();
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = &swapchain;
        present_info.pImageIndices      = frame_image_index;

        VkResult present_result = vkQueuePresentKHR(m_queue_map[Rendering::QueueType::GRAPHIC_QUEUE], &present_info);
        ZENGINE_VALIDATE_ASSERT(present_result == VK_SUCCESS, "Failed to present current frame on Window")

        for (uint32_t i = 0; i < wait_semaphore_collection.size(); ++i)
        {
            wait_semaphore_collection[i]->SetState(Rendering::Primitives::SemaphoreState::Idle);
        }
    }

    Rendering::Pools::CommandPool* VulkanDevice::CreateCommandPool(Rendering::QueueType queue_type, bool present_on_swapchain)
    {
        return m_command_pool_collection.emplace_back(CreateRef<Rendering::Pools::CommandPool>(queue_type, present_on_swapchain)).get();
    }

    const std::vector<Ref<Rendering::Pools::CommandPool>>& VulkanDevice::GetAllCommandPools()
    {
        return m_command_pool_collection;
    }

    void VulkanDevice::DisposeCommandPool(const Rendering::Pools::CommandPool* pool)
    {
        std::vector<Ref<Rendering::Pools::CommandPool>>::iterator it;
        for (it = m_command_pool_collection.begin(); it != m_command_pool_collection.end(); ++it)
        {
            if (it->get() == pool)
            {
                break;
            }
        }

        if (it != m_command_pool_collection.end())
        {
            m_command_pool_collection.erase(it);
        }
    }

    void VulkanDevice::QueueWait(Rendering::QueueType type)
    {
        std::lock_guard lock(m_queue_mutex);
        ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(m_queue_map[type]) == VK_SUCCESS, "Failed to wait on queue")
    }

    QueueView VulkanDevice::GetQueue(Rendering::QueueType type)
    {
        uint32_t queue_family_index = 0;
        switch (type)
        {
            case ZEngine::Rendering::QueueType::GRAPHIC_QUEUE:
                queue_family_index = m_graphic_family_index;
                break;
            case ZEngine::Rendering::QueueType::TRANSFER_QUEUE:
                queue_family_index = m_transfer_family_index;
                break;
        }
        return QueueView{.FamilyIndex = queue_family_index, .Handle = m_queue_map[type]};
    }

    void VulkanDevice::QueueWaitAll()
    {
        QueueWait(Rendering::QueueType::TRANSFER_QUEUE);
        QueueWait(Rendering::QueueType::GRAPHIC_QUEUE);
    }

    VkSurfaceKHR VulkanDevice::GetSurface()
    {
        return m_surface;
    }

    VkSurfaceFormatKHR VulkanDevice::GetSurfaceFormat()
    {
        return m_surface_format;
    }

    VkPresentModeKHR VulkanDevice::GetPresentMode()
    {
        return m_present_mode;
    }

    VkDescriptorPool VulkanDevice::GetDescriptorPool()
    {
        return m_descriptor_pool;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::__debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT             messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       pUserData)
    {
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            ZENGINE_CORE_ERROR(pCallbackData->pMessage)
        }

        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            ZENGINE_CORE_WARN(pCallbackData->pMessage)
        }

        return VK_FALSE;
    }

    VkPhysicalDevice VulkanDevice::GetNativePhysicalDeviceHandle()
    {
        return m_physical_device;
    }

    const VkPhysicalDeviceProperties& VulkanDevice::GetPhysicalDeviceProperties()
    {
        return m_physical_device_properties;
    }

    const VkPhysicalDeviceMemoryProperties& VulkanDevice::GetPhysicalDeviceMemoryProperties()
    {
        return m_physical_device_memory_properties;
    }

    BufferView VulkanDevice::CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VkMemoryPropertyFlags requested_properties)
    {
        BufferView         buffer_view        = {};
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = byte_size;
        buffer_create_info.usage              = buffer_usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        ZENGINE_VALIDATE_ASSERT(vkCreateBuffer(m_logical_device, &buffer_create_info, nullptr, &(buffer_view.Handle)) == VK_SUCCESS, "Failed to create vertex buffer")

        VkMemoryRequirements memory_requirement;
        vkGetBufferMemoryRequirements(m_logical_device, buffer_view.Handle, &memory_requirement);

        bool     memory_type_index_found{false};
        uint32_t memory_type_index{0};
        for (; memory_type_index < m_physical_device_memory_properties.memoryTypeCount; ++memory_type_index)
        {
            if ((memory_requirement.memoryTypeBits & (1 << memory_type_index)) &&
                (m_physical_device_memory_properties.memoryTypes[memory_type_index].propertyFlags & requested_properties) == requested_properties)
            {
                memory_type_index_found = true;
                break;
            }
        }

        if (!memory_type_index_found)
        {
            ZENGINE_CORE_ERROR("Failed to find a valid memory type")
        }

        VkMemoryAllocateInfo memory_allocate_info = {};
        memory_allocate_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize       = memory_requirement.size;
        memory_allocate_info.memoryTypeIndex      = memory_type_index;

        // ToDo : Call of vkAllocateMemory(...) isn't optimal because it is limited by maxMemoryAllocationCount
        // It should be replaced by VulkanMemoryAllocator SDK (see :  https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
        ZENGINE_VALIDATE_ASSERT(
            vkAllocateMemory(m_logical_device, &memory_allocate_info, nullptr, &(buffer_view.Memory)) == VK_SUCCESS, "Failed to allocate memory for vertex buffer")
        ZENGINE_VALIDATE_ASSERT(vkBindBufferMemory(m_logical_device, buffer_view.Handle, buffer_view.Memory, 0) == VK_SUCCESS, "Failed to bind the memory to the vertex buffer")

        return buffer_view;
    }

    void VulkanDevice::CopyBuffer(const BufferView& source, const BufferView& destination, VkDeviceSize byte_size)
    {
        auto command_buffer = BeginInstantCommandBuffer(Rendering::QueueType::TRANSFER_QUEUE);
        {
            VkBufferCopy buffer_copy = {};
            buffer_copy.srcOffset    = 0;
            buffer_copy.dstOffset    = 0;
            buffer_copy.size         = byte_size;

            vkCmdCopyBuffer(command_buffer->GetHandle(), source.Handle, destination.Handle, 1, &buffer_copy);
        }
        EndInstantCommandBuffer(command_buffer);
    }

    BufferImage VulkanDevice::CreateImage(
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
        VkImageAspectFlagBits image_aspect_flag)
    {
        BufferImage       buffer_image      = {};
        VkImageCreateInfo image_create_info = {};
        image_create_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.flags             = 0;
        image_create_info.imageType         = image_type;
        image_create_info.extent.width      = width;
        image_create_info.extent.height     = height;
        image_create_info.extent.depth      = 1;
        image_create_info.mipLevels         = 1;
        image_create_info.arrayLayers       = 1;
        image_create_info.format            = image_format;
        image_create_info.tiling            = image_tiling;
        image_create_info.initialLayout     = image_initial_layout;
        image_create_info.usage             = image_usage;
        image_create_info.sharingMode       = image_sharing_mode;
        image_create_info.samples           = image_sample_count;

        ZENGINE_VALIDATE_ASSERT(vkCreateImage(m_logical_device, &image_create_info, nullptr, &(buffer_image.Handle)) == VK_SUCCESS, "Failed to create image")

        VkMemoryRequirements memory_requirement;
        vkGetImageMemoryRequirements(m_logical_device, buffer_image.Handle, &memory_requirement);

        bool     memory_type_index_found{false};
        uint32_t memory_type_index{0};
        for (; memory_type_index < m_physical_device_memory_properties.memoryTypeCount; ++memory_type_index)
        {
            if ((memory_requirement.memoryTypeBits & (1 << memory_type_index)) &&
                (m_physical_device_memory_properties.memoryTypes[memory_type_index].propertyFlags & requested_properties) == requested_properties)
            {
                memory_type_index_found = true;
                break;
            }
        }

        if (!memory_type_index_found)
        {
            ZENGINE_CORE_ERROR("Failed to find a valid memory type")
        }

        VkMemoryAllocateInfo memory_allocate_info = {};
        memory_allocate_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize       = memory_requirement.size;
        memory_allocate_info.memoryTypeIndex      = memory_type_index;

        // ToDo : Call of vkAllocateMemory(...) isn't optimal because it is limited by maxMemoryAllocationCount
        // It should be replaced by VulkanMemoryAllocator SDK (see :  https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
        ZENGINE_VALIDATE_ASSERT(vkAllocateMemory(m_logical_device, &memory_allocate_info, nullptr, &(buffer_image.Memory)) == VK_SUCCESS, "Failed to allocate memory for image")
        ZENGINE_VALIDATE_ASSERT(vkBindImageMemory(m_logical_device, buffer_image.Handle, buffer_image.Memory, 0) == VK_SUCCESS, "Failed to bind the memory to image")

        buffer_image.ViewHandle = CreateImageView(buffer_image.Handle, image_format, image_aspect_flag);

        return buffer_image;
    }

    VkSampler VulkanDevice::CreateImageSampler()
    {
        VkSampler sampler{VK_NULL_HANDLE};

        VkSamplerCreateInfo sampler_create_info = {};
        sampler_create_info.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create_info.minFilter           = VK_FILTER_LINEAR;
        sampler_create_info.magFilter           = VK_FILTER_NEAREST;
        sampler_create_info.addressModeU        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.addressModeV        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.addressModeW        = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        {
            sampler_create_info.maxAnisotropy = m_physical_device_properties.limits.maxSamplerAnisotropy;
        }
        sampler_create_info.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler_create_info.unnormalizedCoordinates = VK_FALSE;
        sampler_create_info.compareEnable           = VK_FALSE;
        sampler_create_info.compareOp               = VK_COMPARE_OP_ALWAYS;
        sampler_create_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.mipLodBias              = 0.0f;
        sampler_create_info.minLod                  = -1000.0f;
        sampler_create_info.maxLod                  = 1000.0f;

        ZENGINE_VALIDATE_ASSERT(vkCreateSampler(m_logical_device, &sampler_create_info, nullptr, &sampler) == VK_SUCCESS, "Failed to create Texture Sampler")

        return sampler;
    }

    VkFormat VulkanDevice::FindSupportedFormat(const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags)
    {
        VkFormat supported_format = VK_FORMAT_UNDEFINED;
        auto     physical_device  = Hardwares::VulkanDevice::GetNativePhysicalDeviceHandle();
        for (uint32_t i = 0; i < format_collection.size(); ++i)
        {
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(physical_device, format_collection[i], &format_properties);

            if (image_tiling == VK_IMAGE_TILING_LINEAR && (format_properties.linearTilingFeatures & feature_flags) == feature_flags)
            {
                supported_format = format_collection[i];
            }
            else if (image_tiling == VK_IMAGE_TILING_OPTIMAL && (format_properties.optimalTilingFeatures & feature_flags) == feature_flags)
            {
                supported_format = format_collection[i];
            }
        }

        ZENGINE_VALIDATE_ASSERT(supported_format != VK_FORMAT_UNDEFINED, "Failed to find supported Image format")

        return supported_format;
    }

    VkFormat VulkanDevice::FindDepthFormat()
    {
        return FindSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    VkImageView VulkanDevice::CreateImageView(VkImage image, VkFormat image_format, VkImageAspectFlagBits image_aspect_flag)
    {
        VkImageView           image_view{VK_NULL_HANDLE};
        VkImageViewCreateInfo image_view_create_info           = {};
        image_view_create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.format                          = image_format;
        image_view_create_info.image                           = image;
        image_view_create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        image_view_create_info.subresourceRange.aspectMask     = image_aspect_flag;
        image_view_create_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
        image_view_create_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
        image_view_create_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
        image_view_create_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
        image_view_create_info.subresourceRange.baseMipLevel   = 0;
        image_view_create_info.subresourceRange.levelCount     = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount     = 1;

        ZENGINE_VALIDATE_ASSERT(vkCreateImageView(m_logical_device, &image_view_create_info, nullptr, &image_view) == VK_SUCCESS, "Failed to create image view")

        return image_view;
    }

    void VulkanDevice::CopyBufferToImage(const BufferView& source, BufferImage& destination, uint32_t width, uint32_t height)
    {
        auto command_buffer = BeginInstantCommandBuffer(Rendering::QueueType::TRANSFER_QUEUE);
        {
            VkBufferImageCopy buffer_image_copy = {};
            buffer_image_copy.bufferOffset      = 0;
            buffer_image_copy.bufferRowLength   = 0;
            buffer_image_copy.bufferImageHeight = 0;

            buffer_image_copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            buffer_image_copy.imageSubresource.mipLevel       = 0;
            buffer_image_copy.imageSubresource.baseArrayLayer = 0;
            buffer_image_copy.imageSubresource.layerCount     = 1;

            buffer_image_copy.imageOffset = {0, 0, 0};
            buffer_image_copy.imageExtent = {width, height, 1};

            vkCmdCopyBufferToImage(command_buffer->GetHandle(), source.Handle, destination.Handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_copy);
        }
        EndInstantCommandBuffer(command_buffer);
    }

    VkFramebuffer VulkanDevice::CreateFramebuffer(
        const std::vector<VkImageView>& attachments,
        const VkRenderPass&             render_pass,
        uint32_t                        width,
        uint32_t                        height,
        uint32_t                        layer_number)
    {
        VkFramebuffer           framebuffer{VK_NULL_HANDLE};
        VkFramebufferCreateInfo framebuffer_create_info = {};
        framebuffer_create_info.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.renderPass              = render_pass;
        framebuffer_create_info.attachmentCount         = attachments.size();
        framebuffer_create_info.pAttachments            = attachments.data();
        framebuffer_create_info.width                   = width;
        framebuffer_create_info.height                  = height;
        framebuffer_create_info.layers                  = layer_number;

        ZENGINE_VALIDATE_ASSERT(vkCreateFramebuffer(m_logical_device, &framebuffer_create_info, nullptr, &framebuffer) == VK_SUCCESS, "Failed to create Framebuffer")

        return framebuffer;
    }

    Rendering::Buffers::CommandBuffer* VulkanDevice::BeginInstantCommandBuffer(Rendering::QueueType type)
    {
        std::unique_lock lock(m_instant_command_mutex);
        m_cond.wait(lock, [] {
            return !m_is_executing_instant_command;
        });
        m_is_executing_instant_command = true;

        auto command_buffer = m_in_device_command_pool_map[type]->GetCurrentCommmandBuffer();
        command_buffer->Begin();

        return command_buffer;
    }

    void VulkanDevice::EndInstantCommandBuffer(Rendering::Buffers::CommandBuffer* const command)
    {
        ZENGINE_VALIDATE_ASSERT(command != nullptr, "Command buffer can't be null")
        command->End();
        command->Submit();
        command->WaitForExecution();
        {
            std::unique_lock lock(m_instant_command_mutex);
            m_is_executing_instant_command = false;
        }
        m_cond.notify_one();
    }
} // namespace ZEngine::Hardwares
