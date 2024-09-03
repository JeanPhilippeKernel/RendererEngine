#include <pch.h>
#include <ZEngineDef.h>

/*
* We define those Macros before inclusion of VulkanDevice.h so we can enable impl from VMA header
*/
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3

#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>
#include <Helpers/MemoryOperations.h>
#include <Window/CoreWindow.h>


using namespace std::chrono_literals;
using namespace ZEngine::Rendering::Primitives;

namespace ZEngine::Hardwares
{
    std::string                                                                      VulkanDevice::s_application_name                  = "ZEngine";
    VulkanLayer                                                                      VulkanDevice::s_layer                             = {};
    VkInstance                                                                       VulkanDevice::s_vulkan_instance                   = VK_NULL_HANDLE;
    VkSurfaceKHR                                                                     VulkanDevice::s_surface                           = VK_NULL_HANDLE;
    uint32_t                                                                         VulkanDevice::s_graphic_family_index              = 0;
    uint32_t                                                                         VulkanDevice::s_transfer_family_index             = 0;
    uint32_t                                                                         VulkanDevice::s_current_frame_index               = UINT32_MAX;
    std::map<Rendering::QueueType, VkQueue>                                          VulkanDevice::s_queue_map                         = {};
    VkDevice                                                                         VulkanDevice::s_logical_device                    = VK_NULL_HANDLE;
    VkPhysicalDevice                                                                 VulkanDevice::s_physical_device                   = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties                                                       VulkanDevice::s_physical_device_properties        = {};
    VkPhysicalDeviceFeatures                                                         VulkanDevice::s_physical_device_feature           = {};
    VkPhysicalDeviceMemoryProperties                                                 VulkanDevice::s_physical_device_memory_properties = {};
    VkDebugUtilsMessengerEXT                                                         VulkanDevice::s_debug_messenger                   = VK_NULL_HANDLE;
    std::deque<BufferView>                                                           VulkanDevice::s_dirty_buffer_queue                = {};
    std::deque<BufferImage>                                                          VulkanDevice::s_dirty_buffer_image_queue          = {};
    std::vector<Ref<Rendering::Pools::CommandPool>>                                  VulkanDevice::s_command_pool_collection           = {};
    PFN_vkCreateDebugUtilsMessengerEXT                                               VulkanDevice::__createDebugMessengerPtr           = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT                                              VulkanDevice::__destroyDebugMessengerPtr          = nullptr;
    std::vector<VkSurfaceFormatKHR>                                                  VulkanDevice::s_surface_format_collection         = {};
    std::vector<VkPresentModeKHR>                                                    VulkanDevice::s_present_mode_collection           = {};
    VkSurfaceFormatKHR                                                               VulkanDevice::s_surface_format                    = {};
    VkPresentModeKHR                                                                 VulkanDevice::s_present_mode                      = {};
    VkDescriptorPool                                                                 VulkanDevice::s_descriptor_pool                   = VK_NULL_HANDLE;
    VmaAllocator                                                                     VulkanDevice::s_vma_allocator                     = nullptr;
    std::map<Rendering::QueueType, Ref<Rendering::Pools::CommandPool>>               VulkanDevice::s_in_device_command_pool_map        = {};
    std::mutex                                                                       VulkanDevice::s_queue_mutex                       = {};
    std::mutex                                                                       VulkanDevice::s_command_pool_mutex                = {};
    std::mutex                                                                       VulkanDevice::s_deletion_queue_mutex              = {};
    std::mutex                                                                       VulkanDevice::s_frame_value_mutex                 = {};
    std::condition_variable                                                          VulkanDevice::s_cond                              = {};
    std::atomic_bool                                                                 VulkanDevice::s_is_executing_instant_command      = false;
    std::mutex                                                                       VulkanDevice::s_instant_command_mutex             = {};
    std::map<Rendering::QueueType, std::map<uint32_t, std::vector<QueueSubmitInfo>>> VulkanDevice::s_queue_submit_info_pool            = {};
    std::deque<DirtyResource>                                                        VulkanDevice::s_dirty_resource_collection         = {};

    void VulkanDevice::Initialize(const Ref<Window::CoreWindow>& window, const std::vector<const char*>& additional_extension_layer_name_collection)
    {
        /*Create Vulkan Instance*/
        VkApplicationInfo app_info                = {};
        app_info.sType                            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName                 = s_application_name.data();
        app_info.pEngineName                      = s_application_name.data();
        app_info.apiVersion                       = VK_API_VERSION_1_3;
        app_info.engineVersion                    = 1;
        app_info.applicationVersion               = 1;
        app_info.pNext                            = nullptr;
        VkInstanceCreateInfo instance_create_info = {};
        instance_create_info.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo     = &app_info;
        instance_create_info.pNext                = nullptr;
        instance_create_info.flags                = 0;

        auto layer_properties = s_layer.GetInstanceLayerProperties();

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

#ifdef ENABLE_VULKAN_SYNCHRONIZATION_LAYER
        std::unordered_set<std::string> synchronization_layer_collection = {"VK_LAYER_KHRONOS_synchronization2"};
        for (std::string_view layer_name : synchronization_layer_collection)
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

        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &s_vulkan_instance);

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

        __createDebugMessengerPtr  = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(s_vulkan_instance, "vkCreateDebugUtilsMessengerEXT"));
        __destroyDebugMessengerPtr = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(s_vulkan_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (__createDebugMessengerPtr)
        {
            __createDebugMessengerPtr(s_vulkan_instance, &messenger_create_info, nullptr, &s_debug_messenger);
        }

        ZENGINE_VALIDATE_ASSERT(
            glfwCreateWindowSurface(s_vulkan_instance, reinterpret_cast<GLFWwindow*>(window->GetNativeWindow()), nullptr, &s_surface) == VK_SUCCESS,
            "Failed Window Surface from GLFW")

        /*Create Vulkan Device*/
        ZENGINE_VALIDATE_ASSERT(s_vulkan_instance != VK_NULL_HANDLE, "A Vulkan Instance must be created first!")

        uint32_t gpu_device_count{0};
        vkEnumeratePhysicalDevices(s_vulkan_instance, &gpu_device_count, nullptr);

        std::vector<VkPhysicalDevice> physical_device_collection(gpu_device_count);
        vkEnumeratePhysicalDevices(s_vulkan_instance, &gpu_device_count, physical_device_collection.data());

        for (VkPhysicalDevice physical_device : physical_device_collection)
        {
            VkPhysicalDeviceProperties physical_device_properties;
            VkPhysicalDeviceFeatures   physical_device_feature;
            vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
            vkGetPhysicalDeviceFeatures(physical_device, &physical_device_feature);

            if ((physical_device_feature.geometryShader == VK_TRUE) && (physical_device_feature.samplerAnisotropy == VK_TRUE) &&
                ((physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
                 (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)))
            {
                s_physical_device            = physical_device;
                s_physical_device_properties = physical_device_properties;
                s_physical_device_feature    = physical_device_feature;
                vkGetPhysicalDeviceMemoryProperties(s_physical_device, &s_physical_device_memory_properties);
                break;
            }
        }

        std::vector<const char*> requested_device_enabled_layer_name_collection   = {};
        std::vector<const char*> requested_device_extension_layer_name_collection = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME};

        for (LayerProperty& layer : selected_layer_property_collection)
        {
            s_layer.GetExtensionProperties(layer, &s_physical_device);

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
        vkGetPhysicalDeviceQueueFamilyProperties(s_physical_device, &physical_device_queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> physical_device_queue_family_collection;
        physical_device_queue_family_collection.resize(physical_device_queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(s_physical_device, &physical_device_queue_family_count, physical_device_queue_family_collection.data());

        uint32_t                queue_family_index      = 0;
        VkQueueFamilyProperties queue_family_properties = {};
        for (size_t index = 0; index < physical_device_queue_family_count; ++index)
        {
            if (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                // Ensuring presentation support
                if (s_surface)
                {
                    VkBool32 present_support = false;

                    ZENGINE_VALIDATE_ASSERT(
                        vkGetPhysicalDeviceSurfaceSupportKHR(s_physical_device, index, s_surface, &present_support) == VK_SUCCESS,
                        "Failed to get device surface support information")

                    if (present_support)
                    {
                        s_graphic_family_index = index;
                    }
                }
            }
            else if (
                (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
                (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
            {
                s_transfer_family_index = index;
            }
        }

        float                                queue_prorities              = 1.0f;
        auto                                 family_index_collection      = std::set{s_graphic_family_index, s_transfer_family_index};
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
        /*
         * Enabling some features
         */
        s_physical_device_feature.drawIndirectFirstInstance              = VK_TRUE;
        s_physical_device_feature.multiDrawIndirect                      = VK_TRUE;
        s_physical_device_feature.shaderSampledImageArrayDynamicIndexing = VK_TRUE;

        VkPhysicalDeviceDescriptorIndexingFeaturesEXT physical_device_descriptor_indexing_features = {};
        physical_device_descriptor_indexing_features.sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
        physical_device_descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing     = VK_TRUE;
        physical_device_descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind  = VK_TRUE;
        physical_device_descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending     = VK_TRUE;
        physical_device_descriptor_indexing_features.descriptorBindingPartiallyBound               = VK_TRUE;
        physical_device_descriptor_indexing_features.runtimeDescriptorArray                        = VK_TRUE;

        VkPhysicalDeviceFeatures2 device_features_2 = {};
        device_features_2.sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        device_features_2.pNext                     = &physical_device_descriptor_indexing_features;
        device_features_2.features                  = s_physical_device_feature;

        VkDeviceCreateInfo device_create_info    = {};
        device_create_info.sType                 = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount  = queue_create_info_collection.size();
        device_create_info.pQueueCreateInfos     = queue_create_info_collection.data();
        device_create_info.enabledExtensionCount = static_cast<uint32_t>(requested_device_extension_layer_name_collection.size());
        device_create_info.ppEnabledExtensionNames =
            (requested_device_extension_layer_name_collection.size() > 0) ? requested_device_extension_layer_name_collection.data() : nullptr;
        device_create_info.pEnabledFeatures = nullptr;
        device_create_info.pNext            = &device_features_2;

        ZENGINE_VALIDATE_ASSERT(vkCreateDevice(s_physical_device, &device_create_info, nullptr, &s_logical_device) == VK_SUCCESS, "Failed to create GPU logical device")

        /*Create Vulkan Graphic Queue*/
        s_queue_map[Rendering::QueueType::GRAPHIC_QUEUE] = VK_NULL_HANDLE;
        vkGetDeviceQueue(s_logical_device, s_graphic_family_index, 0, &(s_queue_map[Rendering::QueueType::GRAPHIC_QUEUE]));

        /*Create Vulkan Transfer Queue*/
        s_queue_map[Rendering::QueueType::TRANSFER_QUEUE] = VK_NULL_HANDLE;
        vkGetDeviceQueue(s_logical_device, s_transfer_family_index, 0, &(s_queue_map[Rendering::QueueType::TRANSFER_QUEUE]));

        /**/
        uint32_t format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(s_physical_device, s_surface, &format_count, nullptr);
        if (format_count != 0)
        {
            s_surface_format_collection.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(s_physical_device, s_surface, &format_count, s_surface_format_collection.data());
        }

        uint32_t present_mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(s_physical_device, s_surface, &present_mode_count, nullptr);
        if (present_mode_count != 0)
        {
            s_present_mode_collection.resize(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(s_physical_device, s_surface, &present_mode_count, s_present_mode_collection.data());
        }

        for (const VkSurfaceFormatKHR& format_khr : s_surface_format_collection)
        {
            // default is: VK_FORMAT_B8G8R8A8_SRGB
            // but Imgui wants : VK_FORMAT_B8G8R8A8_UNORM ...
            if ((format_khr.format == VK_FORMAT_B8G8R8A8_UNORM) && (format_khr.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
            {
                s_surface_format = format_khr;
                break;
            }
        }

        /* Present Mode selection */
        if (window->IsVSyncEnable())
        {
            s_present_mode = VK_PRESENT_MODE_FIFO_KHR;
            for (const VkPresentModeKHR present_mode_khr : s_present_mode_collection)
            {
                if (present_mode_khr == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    s_present_mode = present_mode_khr;
                    break;
                }
            }
        }
        else
        {
            s_present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            for (const VkPresentModeKHR present_mode_khr : s_present_mode_collection)
            {
                if (present_mode_khr == VK_PRESENT_MODE_IMMEDIATE_KHR)
                {
                    s_present_mode = present_mode_khr;
                    break;
                }
            }
        }

        /*In Device Command Pool*/
        s_in_device_command_pool_map[Rendering::QueueType::GRAPHIC_QUEUE]  = CreateRef<Rendering::Pools::CommandPool>(Rendering::QueueType::GRAPHIC_QUEUE, 0, false);
        s_in_device_command_pool_map[Rendering::QueueType::TRANSFER_QUEUE] = CreateRef<Rendering::Pools::CommandPool>(Rendering::QueueType::TRANSFER_QUEUE, 0, false);

        /*
         * Creating VMA Allocators
         */
        VmaAllocatorCreateInfo vma_allocator_create_info = {};
        vma_allocator_create_info.device                 = s_logical_device;
        vma_allocator_create_info.physicalDevice         = s_physical_device;
        vma_allocator_create_info.instance               = s_vulkan_instance;
        vma_allocator_create_info.vulkanApiVersion       = VK_API_VERSION_1_3;
        ZENGINE_VALIDATE_ASSERT(vmaCreateAllocator(&vma_allocator_create_info, &s_vma_allocator) == VK_SUCCESS, "Failed to create VMA Allocator")
    }

    void VulkanDevice::Deinitialize()
    {
        QueueWaitAll();

        while (!s_dirty_buffer_queue.empty())
        {
            __cleanupBufferDirtyResource();
        }

        while (!s_dirty_buffer_image_queue.empty())
        {
            __cleanupBufferImageDirtyResource();
        }

        while (!s_dirty_resource_collection.empty())
        {
            __cleanupDirtyResource();
        }

        s_in_device_command_pool_map.clear();
        s_queue_submit_info_pool.clear();
        ZENGINE_DESTROY_VULKAN_HANDLE(s_vulkan_instance, vkDestroySurfaceKHR, s_surface, nullptr)
    }

    void VulkanDevice::Dispose()
    {
        vmaDestroyAllocator(s_vma_allocator);

        if (__destroyDebugMessengerPtr)
        {
            ZENGINE_DESTROY_VULKAN_HANDLE(s_vulkan_instance, __destroyDebugMessengerPtr, s_debug_messenger, nullptr)
            __destroyDebugMessengerPtr = nullptr;
            __createDebugMessengerPtr  = nullptr;
        }
        vkDestroyDevice(s_logical_device, nullptr);
        vkDestroyInstance(s_vulkan_instance, nullptr);

        s_logical_device           = VK_NULL_HANDLE;
        s_vulkan_instance          = VK_NULL_HANDLE;
    }

    VkDevice VulkanDevice::GetNativeDeviceHandle()
    {
        return s_logical_device;
    }

    VkInstance VulkanDevice::GetNativeInstanceHandle()
    {
        return s_vulkan_instance;
    }

    bool VulkanDevice::QueueSubmit(
        Rendering::QueueType                    queue_type,
        const VkPipelineStageFlags              wait_stage_flag,
        Rendering::Buffers::CommandBuffer&      command_buffer,
        bool                                    as_instant_submission,
        Rendering::Primitives::Semaphore* const wait_semaphore,
        Rendering::Primitives::Semaphore* const,
        Rendering::Primitives::Fence* const)
    {
        std::lock_guard lock(s_queue_mutex);

        if (!command_buffer.GetHandle())
        {
            ZENGINE_CORE_ERROR("Command Buffer handle is null, Failed to submit to Queue")
            return false;
        }

        QueueSubmitInfo q      = {.Buffer = command_buffer};
        q.DestinationStageMask = wait_stage_flag;
        q.Type                 = queue_type;
        q.IsImmediate          = as_instant_submission;

        if (!q.IsImmediate)
        {
            s_queue_submit_info_pool[queue_type][wait_stage_flag].push_back(std::move(q));
            return true;
        }

        Ref<Semaphore> signal_semaphore = CreateRef<Semaphore>();
        Ref<Fence>     signal_fence     = CreateRef<Fence>();

        ZENGINE_VALIDATE_ASSERT(signal_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")
        ZENGINE_VALIDATE_ASSERT(signal_fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")

        std::array<VkSemaphore, 1> signal_semaphore_collection = {signal_semaphore->GetHandle()};
        VkCommandBuffer            buffer_handle               = q.Buffer.GetHandle();
        VkSubmitInfo               submit_info                 = {};
        submit_info.sType                                      = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext                                      = nullptr;
        submit_info.waitSemaphoreCount                         =  0;
        submit_info.pWaitSemaphores                            = nullptr;
        submit_info.signalSemaphoreCount                       = 1;
        submit_info.pSignalSemaphores                          = signal_semaphore_collection.data();
        submit_info.pWaitDstStageMask                          = &wait_stage_flag;
        submit_info.commandBufferCount                         = 1;
        submit_info.pCommandBuffers                            = &buffer_handle;

        ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(GetQueue(queue_type).Handle, 1, &submit_info, signal_fence->GetHandle()) == VK_SUCCESS, "Failed to submit queue")
        q.Buffer.SetSignalFence(signal_fence);
        q.Buffer.SetSignalSemaphore(signal_semaphore);
        q.Buffer.SetState(Rendering::Buffers::Pending);

        signal_fence->SetState(FenceState::Submitted);
        signal_semaphore->SetState(SemaphoreState::Submitted);

        if (!signal_fence->Wait())
        {
            ZENGINE_CORE_WARN("Failed to wait for Command buffer's Fence, due to timeout")
        }

        if (q.Buffer.Completed())
        {
            signal_fence->Reset();
        }

        q.Buffer.SetState(Rendering::Buffers::Invalid);

        return true;
    }

    void VulkanDevice::SetCurrentFrameIndex(uint32_t frame)
    {
        {
            std::unique_lock lock(s_frame_value_mutex);
            s_current_frame_index = frame;
        }
    }

    uint32_t VulkanDevice::GetCurrentFrameIndex()
    {
        std::unique_lock lock(s_frame_value_mutex);
        return s_current_frame_index;
    }

    void VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const handle)
    {
        {
            std::lock_guard lock(s_deletion_queue_mutex);
            if (handle)
            {
                auto find_it =  std::find_if(s_dirty_resource_collection.begin(), s_dirty_resource_collection.end(), [handle](const DirtyResource& res) {
                    return res.Handle == handle;
                });

                if (find_it == std::end(s_dirty_resource_collection))
                {
                    s_dirty_resource_collection.push_back(DirtyResource{.FrameIndex = s_current_frame_index, .Handle = handle, .MarkedAsDirtyTime = std::chrono::steady_clock::now(), .Type = resource_type});
                }
            }
        }
    }

    void VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource)
    {
        {
            std::lock_guard lock(s_deletion_queue_mutex);
            if (resource.Handle)
            {
                resource.FrameIndex        = s_current_frame_index;
                resource.Type = resource_type;
                resource.MarkedAsDirtyTime = std::chrono::steady_clock::now();

                auto find_it = std::find_if(s_dirty_resource_collection.begin(), s_dirty_resource_collection.end(), [&resource](const DirtyResource& res) {
                    return res.Handle == resource.Handle;
                });

                if (find_it == std::end(s_dirty_resource_collection))
                {
                    s_dirty_resource_collection.push_back(resource);
                }
            }
        }
    }

    void VulkanDevice::EnqueueBufferForDeletion(BufferView& buffer)
    {
        {
            std::lock_guard lock(s_deletion_queue_mutex);

            buffer.FrameIndex        = s_current_frame_index;
            buffer.MarkedAsDirtyTime = std::chrono::steady_clock::now();
            s_dirty_buffer_queue.push_back(buffer);
        }
    }

    void VulkanDevice::EnqueueBufferImageForDeletion(BufferImage& buffer)
    {
        {
            std::lock_guard lock(s_deletion_queue_mutex);

            buffer.FrameIndex        = s_current_frame_index;
            buffer.MarkedAsDirtyTime = std::chrono::steady_clock::now();
            s_dirty_buffer_image_queue.push_back(buffer);
        }
    }

    bool VulkanDevice::Present(
        VkSwapchainKHR                          swapchain,
        uint32_t*                               frame_image_index,
        Rendering::Primitives::Semaphore* const wait_semaphore,
        Rendering::Primitives::Semaphore* const render_complete_semaphore,
        Rendering::Primitives::Fence* const     frame_fence)
    {
        std::vector<VkSemaphore> wait_semaphore_handle_collection   = {wait_semaphore->GetHandle()};
        std::vector<VkSemaphore> signal_semaphore_handle_collection = {render_complete_semaphore->GetHandle()};

        /*
         * Submitting pending Command Buffer
         */
        std::vector<QueueSubmission> queue_submission_collection = {};
        auto&                        pending_cmb_collection      = s_queue_submit_info_pool[Rendering::QueueType::GRAPHIC_QUEUE][VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT];

        std::vector<VkCommandBuffer> buffers = {};
        std::transform(pending_cmb_collection.begin(), pending_cmb_collection.end(), std::back_inserter(buffers), [&](const QueueSubmitInfo& info) {
            info.Buffer.SetSignalSemaphore(render_complete_semaphore);
            info.Buffer.SetSignalFence(frame_fence);
            return info.Buffer.GetHandle();
        });

        ZENGINE_VALIDATE_ASSERT(render_complete_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")
        ZENGINE_VALIDATE_ASSERT(frame_fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")

        VkPipelineStageFlags stage_flag  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         submit_info = {};
        submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext                = nullptr;
        submit_info.waitSemaphoreCount   = wait_semaphore_handle_collection.size();
        submit_info.pWaitSemaphores      = wait_semaphore_handle_collection.data();
        submit_info.signalSemaphoreCount = signal_semaphore_handle_collection.size();
        submit_info.pSignalSemaphores    = signal_semaphore_handle_collection.data();
        submit_info.pWaitDstStageMask    = &(stage_flag);
        submit_info.commandBufferCount   = buffers.size();
        submit_info.pCommandBuffers      = buffers.data();

        ZENGINE_VALIDATE_ASSERT(
            vkQueueSubmit(GetQueue(Rendering::QueueType::GRAPHIC_QUEUE).Handle, 1, &(submit_info), frame_fence->GetHandle()) == VK_SUCCESS, "Failed to submit queue")

        for (auto&& pending_cmb : pending_cmb_collection)
        {
            pending_cmb.Buffer.SetState(Rendering::Buffers::Pending);
        }

        frame_fence->SetState(FenceState::Submitted);
        render_complete_semaphore->SetState(SemaphoreState::Submitted);

        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = signal_semaphore_handle_collection.size();
        present_info.pWaitSemaphores    = signal_semaphore_handle_collection.data();
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = &swapchain;
        present_info.pImageIndices      = frame_image_index;

        VkResult present_result = vkQueuePresentKHR(s_queue_map[Rendering::QueueType::GRAPHIC_QUEUE], &present_info);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
        {
            return false;
        }

        ZENGINE_VALIDATE_ASSERT(present_result == VK_SUCCESS, "Failed to present current frame on Window")

        wait_semaphore->SetState(Rendering::Primitives::SemaphoreState::Idle);
        render_complete_semaphore->SetState(Rendering::Primitives::SemaphoreState::Idle);

        /*
        * Cleanup current Frame allocated resource
        */
        s_queue_submit_info_pool.clear();

        for (auto it = s_dirty_resource_collection.begin(); it != s_dirty_resource_collection.end();)
        {
            if (it->FrameIndex == *frame_image_index)
            {
                switch (it->Type)
                {
                    case Rendering::DeviceResourceType::SAMPLER:
                        vkDestroySampler(s_logical_device, reinterpret_cast<VkSampler>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::FRAMEBUFFER:
                        vkDestroyFramebuffer(s_logical_device, reinterpret_cast<VkFramebuffer>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::IMAGEVIEW:
                        vkDestroyImageView(s_logical_device, reinterpret_cast<VkImageView>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::IMAGE:
                        vkDestroyImage(s_logical_device, reinterpret_cast<VkImage>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::RENDERPASS:
                        vkDestroyRenderPass(s_logical_device, reinterpret_cast<VkRenderPass>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::BUFFERMEMORY:
                        vkFreeMemory(s_logical_device, reinterpret_cast<VkDeviceMemory>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::BUFFER:
                        vkDestroyBuffer(s_logical_device, reinterpret_cast<VkBuffer>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::PIPELINE_LAYOUT:
                        vkDestroyPipelineLayout(s_logical_device, reinterpret_cast<VkPipelineLayout>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::PIPELINE:
                        vkDestroyPipeline(s_logical_device, reinterpret_cast<VkPipeline>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT:
                        vkDestroyDescriptorSetLayout(s_logical_device, reinterpret_cast<VkDescriptorSetLayout>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::DESCRIPTORPOOL:
                        vkDestroyDescriptorPool(s_logical_device, reinterpret_cast<VkDescriptorPool>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::SEMAPHORE:
                        vkDestroySemaphore(s_logical_device, reinterpret_cast<VkSemaphore>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::FENCE:
                        vkDestroyFence(s_logical_device, reinterpret_cast<VkFence>(it->Handle), nullptr);
                        break;
                    case Rendering::DeviceResourceType::DESCRIPTORSET:
                    {
                        auto ds = reinterpret_cast<VkDescriptorSet>(it->Handle);
                        vkFreeDescriptorSets(s_logical_device, reinterpret_cast<VkDescriptorPool>(it->Data1), 1, &ds);
                        break;
                    }
                }

                it = s_dirty_resource_collection.erase(it);
            }
            else
            {
                it = std::next(it);
            }
        }

        for (auto it = s_dirty_buffer_queue.begin(); it != s_dirty_buffer_queue.end();)
        {
            if (it->FrameIndex == *frame_image_index)
            {
                vmaDestroyBuffer(s_vma_allocator, it->Handle, it->Allocation);
                it = s_dirty_buffer_queue.erase(it);
            }
            else
            {
                it = std::next(it);
            }
        }

        for (auto it = s_dirty_buffer_image_queue.begin(); it != s_dirty_buffer_image_queue.end();)
        {
            if (it->FrameIndex == *frame_image_index)
            {
                vkDestroyImageView(s_logical_device, it->ViewHandle, nullptr);
                vkDestroySampler(s_logical_device, it->Sampler, nullptr);
                vmaDestroyImage(s_vma_allocator, it->Handle, it->Allocation);
                it = s_dirty_buffer_image_queue.erase(it);
            }
            else
            {
                it = std::next(it);
            }
        }

        return true;
    }

    Rendering::Pools::CommandPool* VulkanDevice::CreateCommandPool(Rendering::QueueType queue_type, uint64_t swapchain_id, bool present_on_swapchain)
    {
        auto pool = CreateRef<Rendering::Pools::CommandPool>(queue_type, swapchain_id, present_on_swapchain);
        s_command_pool_collection.push_back(std::move(pool));
        return s_command_pool_collection.back().get();
    }

    void VulkanDevice::DisposeCommandPool(const Rendering::Pools::CommandPool* pool)
    {
        std::vector<Ref<Rendering::Pools::CommandPool>>::iterator it;
        for (it = s_command_pool_collection.begin(); it != s_command_pool_collection.end(); ++it)
        {
            if (it->get() == pool)
            {
                break;
            }
        }

        if (it != s_command_pool_collection.end())
        {
            s_command_pool_collection.erase(it);
        }
    }

    void VulkanDevice::QueueWait(Rendering::QueueType type)
    {
        std::lock_guard lock(s_queue_mutex);
        ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(s_queue_map[type]) == VK_SUCCESS, "Failed to wait on queue")
    }

    QueueView VulkanDevice::GetQueue(Rendering::QueueType type)
    {
        uint32_t queue_family_index = 0;
        switch (type)
        {
            case ZEngine::Rendering::QueueType::GRAPHIC_QUEUE:
                queue_family_index = s_graphic_family_index;
                break;
            case ZEngine::Rendering::QueueType::TRANSFER_QUEUE:
                queue_family_index = s_transfer_family_index;
                break;
        }
        return QueueView{.FamilyIndex = queue_family_index, .Handle = s_queue_map[type]};
    }

    Rendering::Pools::CommandPool* VulkanDevice::GetCommandPool(Rendering::QueueType queue_type)
    {
        return s_in_device_command_pool_map[queue_type].get();
    }

    void VulkanDevice::QueueWaitAll()
    {
        QueueWait(Rendering::QueueType::TRANSFER_QUEUE);
        QueueWait(Rendering::QueueType::GRAPHIC_QUEUE);
    }

    VkSurfaceKHR VulkanDevice::GetSurface()
    {
        return s_surface;
    }

    VkSurfaceFormatKHR VulkanDevice::GetSurfaceFormat()
    {
        return s_surface_format;
    }

    VkPresentModeKHR VulkanDevice::GetPresentMode()
    {
        return s_present_mode;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::__debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT             messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*                                       pUserData)
    {
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            ZENGINE_CORE_ERROR("{}", pCallbackData->pMessage)
        }

        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            ZENGINE_CORE_WARN("{}", pCallbackData->pMessage)
        }

        return VK_FALSE;
    }

    void VulkanDevice::__cleanupDirtyResource()
    {
        auto& resource = s_dirty_resource_collection.front();
        switch (resource.Type)
        {
            case Rendering::DeviceResourceType::SAMPLER:
                vkDestroySampler(s_logical_device, reinterpret_cast<VkSampler>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::FRAMEBUFFER:
                vkDestroyFramebuffer(s_logical_device, reinterpret_cast<VkFramebuffer>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::IMAGEVIEW:
                vkDestroyImageView(s_logical_device, reinterpret_cast<VkImageView>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::IMAGE:
                vkDestroyImage(s_logical_device, reinterpret_cast<VkImage>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::RENDERPASS:
                vkDestroyRenderPass(s_logical_device, reinterpret_cast<VkRenderPass>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::BUFFERMEMORY:
                vkFreeMemory(s_logical_device, reinterpret_cast<VkDeviceMemory>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::BUFFER:
                vkDestroyBuffer(s_logical_device, reinterpret_cast<VkBuffer>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::PIPELINE_LAYOUT:
                vkDestroyPipelineLayout(s_logical_device, reinterpret_cast<VkPipelineLayout>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::PIPELINE:
                vkDestroyPipeline(s_logical_device, reinterpret_cast<VkPipeline>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT:
                vkDestroyDescriptorSetLayout(s_logical_device, reinterpret_cast<VkDescriptorSetLayout>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::DESCRIPTORPOOL:
                vkDestroyDescriptorPool(s_logical_device, reinterpret_cast<VkDescriptorPool>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::SEMAPHORE:
                vkDestroySemaphore(s_logical_device, reinterpret_cast<VkSemaphore>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::FENCE:
                vkDestroyFence(s_logical_device, reinterpret_cast<VkFence>(resource.Handle), nullptr);
                break;
            case Rendering::DeviceResourceType::DESCRIPTORSET:
            {
                auto ds = reinterpret_cast<VkDescriptorSet>(resource.Handle);
                vkFreeDescriptorSets(s_logical_device, reinterpret_cast<VkDescriptorPool>(resource.Data1), 1, &ds);
                break;
            }
        }

        s_dirty_resource_collection.pop_front();
    }

    void VulkanDevice::__cleanupBufferDirtyResource()
    {
        auto& resource = s_dirty_buffer_queue.front();
        vmaDestroyBuffer(s_vma_allocator, resource.Handle, resource.Allocation);
        s_dirty_buffer_queue.pop_front();
    }

    void VulkanDevice::__cleanupBufferImageDirtyResource()
    {
        auto& resource = s_dirty_buffer_image_queue.front();
        vkDestroyImageView(s_logical_device, resource.ViewHandle, nullptr);
        vkDestroySampler(s_logical_device, resource.Sampler, nullptr);
        vmaDestroyImage(s_vma_allocator, resource.Handle, resource.Allocation);

        s_dirty_buffer_image_queue.pop_front();
    }

    VkPhysicalDevice VulkanDevice::GetNativePhysicalDeviceHandle()
    {
        return s_physical_device;
    }

    const VkPhysicalDeviceProperties& VulkanDevice::GetPhysicalDeviceProperties()
    {
        return s_physical_device_properties;
    }

    const VkPhysicalDeviceMemoryProperties& VulkanDevice::GetPhysicalDeviceMemoryProperties()
    {
        return s_physical_device_memory_properties;
    }

    void VulkanDevice::MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data)
    {
        void* mapped_memory;
        if (data)
        {
            ZENGINE_VALIDATE_ASSERT(vmaMapMemory(s_vma_allocator, buffer.Allocation, &mapped_memory) == VK_SUCCESS, "Failed to map memory")
            ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(mapped_memory, data_size, data, data_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            vmaUnmapMemory(s_vma_allocator, buffer.Allocation);
        }
    }

    BufferView VulkanDevice::CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags vma_create_flags)
    {
        BufferView         buffer_view        = {};
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = byte_size;
        buffer_create_info.usage              = buffer_usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO;
        allocation_create_info.flags                   = vma_create_flags;

        ZENGINE_VALIDATE_ASSERT(
            vmaCreateBuffer(s_vma_allocator, &buffer_create_info, &allocation_create_info, &(buffer_view.Handle), &(buffer_view.Allocation), nullptr) == VK_SUCCESS,
            "Failed to create buffer");
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
        VkImageViewType       image_view_type,
        VkFormat              image_format,
        VkImageTiling         image_tiling,
        VkImageLayout         image_initial_layout,
        VkImageUsageFlags     image_usage,
        VkSharingMode         image_sharing_mode,
        VkSampleCountFlagBits image_sample_count,
        VkMemoryPropertyFlags requested_properties,
        VkImageAspectFlagBits image_aspect_flag,
        uint32_t              layer_count,
        VkImageCreateFlags    image_create_flag_bit)
    {
        BufferImage       buffer_image      = {};
        VkImageCreateInfo image_create_info = {};
        image_create_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.flags             = image_create_flag_bit;
        image_create_info.imageType         = image_type;
        image_create_info.extent.width      = width;
        image_create_info.extent.height     = height;
        image_create_info.extent.depth      = 1;
        image_create_info.mipLevels         = 1;
        image_create_info.arrayLayers       = layer_count;
        image_create_info.format            = image_format;
        image_create_info.tiling            = image_tiling;
        image_create_info.initialLayout     = image_initial_layout;
        image_create_info.usage             = image_usage;
        image_create_info.sharingMode       = image_sharing_mode;
        image_create_info.samples           = image_sample_count;

        VmaAllocationCreateInfo allocation_create_info = {};
        // allocation_create_info.requiredFlags           = requested_properties;
        allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;

        ZENGINE_VALIDATE_ASSERT(
            vmaCreateImage(s_vma_allocator, &image_create_info, &allocation_create_info, &(buffer_image.Handle), &(buffer_image.Allocation), nullptr) == VK_SUCCESS,
            "Failed to create buffer");

        buffer_image.ViewHandle = CreateImageView(buffer_image.Handle, image_format, image_view_type, image_aspect_flag, layer_count);
        buffer_image.Sampler    = CreateImageSampler();
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
            sampler_create_info.maxAnisotropy = s_physical_device_properties.limits.maxSamplerAnisotropy;
        }
        sampler_create_info.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler_create_info.unnormalizedCoordinates = VK_FALSE;
        sampler_create_info.compareEnable           = VK_FALSE;
        sampler_create_info.compareOp               = VK_COMPARE_OP_ALWAYS;
        sampler_create_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.mipLodBias              = 0.0f;
        sampler_create_info.minLod                  = -1000.0f;
        sampler_create_info.maxLod                  = 1000.0f;

        ZENGINE_VALIDATE_ASSERT(vkCreateSampler(s_logical_device, &sampler_create_info, nullptr, &sampler) == VK_SUCCESS, "Failed to create Texture Sampler")

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

    VkImageView VulkanDevice::CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count)
    {
        VkImageView           image_view{VK_NULL_HANDLE};
        VkImageViewCreateInfo image_view_create_info           = {};
        image_view_create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        image_view_create_info.format                          = image_format;
        image_view_create_info.image                           = image;
        image_view_create_info.viewType                        = image_view_type;
        image_view_create_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
        image_view_create_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
        image_view_create_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
        image_view_create_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
        image_view_create_info.subresourceRange.aspectMask     = image_aspect_flag;
        image_view_create_info.subresourceRange.baseMipLevel   = 0;
        image_view_create_info.subresourceRange.levelCount     = 1;
        image_view_create_info.subresourceRange.baseArrayLayer = 0;
        image_view_create_info.subresourceRange.layerCount     = layer_count;

        ZENGINE_VALIDATE_ASSERT(vkCreateImageView(s_logical_device, &image_view_create_info, nullptr, &image_view) == VK_SUCCESS, "Failed to create image view")

        return image_view;
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

        ZENGINE_VALIDATE_ASSERT(vkCreateFramebuffer(s_logical_device, &framebuffer_create_info, nullptr, &framebuffer) == VK_SUCCESS, "Failed to create Framebuffer")

        return framebuffer;
    }

    Ref<Rendering::Buffers::CommandBuffer> VulkanDevice::BeginInstantCommandBuffer(Rendering::QueueType type)
    {
        std::unique_lock lock(s_instant_command_mutex);
        s_cond.wait(lock, [] {
            return !s_is_executing_instant_command;
        });
        s_is_executing_instant_command = true;

        auto command_buffer = s_in_device_command_pool_map[type]->GetOneTimeCommmandBuffer();
        command_buffer->Begin();

        return command_buffer;
    }

    void VulkanDevice::EndInstantCommandBuffer(Ref<Rendering::Buffers::CommandBuffer>& command)
    {
        ZENGINE_VALIDATE_ASSERT(command != nullptr, "Command buffer can't be null")
        command->End();
        command->Submit(true);
        {
            std::unique_lock lock(s_instant_command_mutex);
            s_is_executing_instant_command = false;
        }
        s_cond.notify_one();
    }

    VmaAllocator VulkanDevice::GetVmaAllocator()
    {
        return s_vma_allocator;
    }
} // namespace ZEngine::Hardwares
