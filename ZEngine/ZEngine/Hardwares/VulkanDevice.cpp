#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Pools/CommandPool.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/Attachment.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <cstdlib>
#include <filesystem>

using namespace std::chrono_literals;
using namespace ZEngine::Rendering::Primitives;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Hardwares
{
    void AsyncGPUOperation::Initialize(VulkanDevice* device, uint32_t total_buffer_count)
    {
        NextValue = 0;
        Timeline  = ZPushStructCtorArgs(device->Arena, Semaphore, device, true);
        RetireValues.init(device->Arena, total_buffer_count, total_buffer_count);
    }

    void VulkanDevice::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::CoreWindow* const window, uint32_t worker_thread_count)
    {
        Arena                     = arena;
        CurrentWindow             = window;
        WorkerThreadCount         = worker_thread_count;
        ShaderReservedBindingSets = {1}; // Todo: we should introduce HashSet<>
        CommandBufferMgr          = ZPushStructCtor(Arena, CommandBufferManager);
        SwapchainPtr              = ZPushStructCtor(Arena, DeviceSwapchain);

        DefaultDepthFormats.init(Arena, 3);
        DefaultDepthFormats.push(VK_FORMAT_D32_SFLOAT);
        DefaultDepthFormats.push(VK_FORMAT_D32_SFLOAT_S8_UINT);
        DefaultDepthFormats.push(VK_FORMAT_D24_UNORM_S8_UINT);

        ShaderManager.Initialize(arena, 300);

        ShaderCaches.init(arena, 10);
        m_queue_map.init(arena, 4);

        m_layer.QueryInstanceLayerProperties(arena);

        /*Create Vulkan Instance*/
        VkApplicationInfo    app_info             = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pNext = VK_NULL_HANDLE, .pApplicationName = ApplicationName, .applicationVersion = 1, .pEngineName = EngineName, .engineVersion = 1, .apiVersion = VK_API_VERSION_1_3};

        VkInstanceCreateInfo instance_create_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pNext = VK_NULL_HANDLE, .flags = 0, .pApplicationInfo = &app_info};

#ifdef __APPLE__
        instance_create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        auto                 scratch = ZGetScratch(Arena);

        Array<const char*>   enabled_layer_name_collection;
        Array<LayerProperty> selected_layer_property_collection;
        enabled_layer_name_collection.init(scratch.Arena, 10);
        selected_layer_property_collection.init(scratch.Arena, 4);

#ifdef ENABLE_VULKAN_VALIDATION_LAYER

        Array<const char*> validation_layer_name_collection;
        validation_layer_name_collection.init(scratch.Arena, 4);
        validation_layer_name_collection.push("VK_LAYER_KHRONOS_validation");
        validation_layer_name_collection.push("VK_LAYER_LUNARG_monitor");
        // api_dump intentionally excluded — enable via VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump
        // when inspecting a specific failing call. Leaving it in floods logs at 50+ MB/run.
#ifndef __APPLE__
        validation_layer_name_collection.push("VK_LAYER_LUNARG_screenshot");
#endif

        for (const char* layer_name : validation_layer_name_collection)
        {
            auto find_it = std::find_if(std::begin(m_layer.InstanceLayers), std::end(m_layer.InstanceLayers), [&](const LayerProperty& layer_property) { return std::string_view(layer_property.Properties.layerName) == layer_name; });
            if (find_it == std::end(m_layer.InstanceLayers))
            {
                continue;
            }

            enabled_layer_name_collection.push(find_it->Properties.layerName);
            selected_layer_property_collection.push(*find_it);
        }
#endif

#ifdef ENABLE_VULKAN_SYNCHRONIZATION_LAYER

        Array<const char*> synchronization_layer_collection;
        synchronization_layer_collection.init(scratch.Arena, 1);
        synchronization_layer_collection.push("VK_LAYER_KHRONOS_synchronization2");
        for (const char* layer_name : synchronization_layer_collection)
        {
            auto find_it = std::find_if(std::begin(m_layer.InstanceLayers), std::end(m_layer.InstanceLayers), [&](const LayerProperty& layer_property) { return std::string_view(layer_property.Properties.layerName) == layer_name; });
            if (find_it == std::end(m_layer.InstanceLayers))
            {
                continue;
            }

            enabled_layer_name_collection.push(find_it->Properties.layerName);
            selected_layer_property_collection.push(*find_it);
        }
#endif

        Array<const char*> enabled_extension_layer_name_collection;
        enabled_extension_layer_name_collection.init(scratch.Arena, 5);

        for (const LayerProperty& layer : selected_layer_property_collection)
        {
            for (const auto& extension : layer.ExtensionCollection)
            {
                if (std::string_view(extension.extensionName) == "VK_EXT_validation_features" || std::string_view(extension.extensionName) == "VK_EXT_layer_settings")
                    continue;
                else
                    enabled_extension_layer_name_collection.push(extension.extensionName);
            }
        }

        if (!window->RequiredExtensionLayers.empty())
        {
            for (const auto& extension : window->RequiredExtensionLayers)
            {
                enabled_extension_layer_name_collection.push(extension);
            }
        }

#ifdef __APPLE__
        enabled_extension_layer_name_collection.push(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        enabled_extension_layer_name_collection.push(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
#endif
        instance_create_info.enabledLayerCount       = enabled_layer_name_collection.size();
        instance_create_info.ppEnabledLayerNames     = enabled_layer_name_collection.data();
        instance_create_info.enabledExtensionCount   = enabled_extension_layer_name_collection.size();
        instance_create_info.ppEnabledExtensionNames = enabled_extension_layer_name_collection.data();

#ifdef __APPLE__
        // Metal argument buffers require useResource tracking for every bindless texture slot.
        // MoltenVK 1.4 does not mark all textures in a large bindless array as used on the encoder,
        // causing MTLResourceUsage faults when sampling textures added after the first draw.
        // Disable argument buffers to use the explicit resource-binding path.
        VkBool32          mvk_arg_buffers = VK_FALSE;
        VkLayerSettingEXT mvk_settings[]  = {
            {
             .pLayerName   = "MoltenVK",
             .pSettingName = "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS",
             .type         = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
             .valueCount   = 1,
             .pValues      = &mvk_arg_buffers,
             }
        };
        VkLayerSettingsCreateInfoEXT mvk_layer_settings_create_info = {
            .sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
            .pNext        = nullptr,
            .settingCount = 1,
            .pSettings    = mvk_settings,
        };
        instance_create_info.pNext = &mvk_layer_settings_create_info;
#endif

        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &Instance);

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
        VkDebugUtilsMessengerCreateInfoEXT messenger_create_info  = {};
        messenger_create_info.sType                               = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_create_info.messageSeverity                     = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        messenger_create_info.messageSeverity                    |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        messenger_create_info.messageType                         = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger_create_info.pfnUserCallback                     = __debugCallback;
        messenger_create_info.pUserData                           = nullptr; // Optional

        __createDebugMessengerPtr                                 = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));
        __destroyDebugMessengerPtr                                = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (__createDebugMessengerPtr)
        {
            __createDebugMessengerPtr(Instance, &messenger_create_info, nullptr, &m_debug_messenger);
        }

        ZENGINE_VALIDATE_ASSERT(window->CreateSurface(Instance, reinterpret_cast<void**>(&Surface)), "Failed Window Surface from GLFW")

        /*Create Vulkan Device*/
        ZENGINE_VALIDATE_ASSERT(Instance != VK_NULL_HANDLE, "A Vulkan Instance must be created first!")

        uint32_t gpu_device_count{0};
        vkEnumeratePhysicalDevices(Instance, &gpu_device_count, nullptr);

        Array<VkPhysicalDevice> physical_device_collection;
        physical_device_collection.init(scratch.Arena, gpu_device_count, gpu_device_count);
        vkEnumeratePhysicalDevices(Instance, &gpu_device_count, physical_device_collection.data());

        auto try_select_device = [&](VkPhysicalDeviceType preferred_type) {
            for (VkPhysicalDevice physical_device : physical_device_collection)
            {
                VkPhysicalDeviceVulkan12Properties vulkan_1_2_properties = {};
                vulkan_1_2_properties.sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;

                VkPhysicalDeviceProperties2 physical_device_properties   = {};
                physical_device_properties.sType                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                physical_device_properties.pNext                         = &vulkan_1_2_properties;

                vkGetPhysicalDeviceProperties2(physical_device, &physical_device_properties);

                VkPhysicalDeviceVulkan12Features vulkan_1_2_features = {};
                vulkan_1_2_features.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

                VkPhysicalDeviceFeatures2 physical_device_feature    = {};
                physical_device_feature.sType                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                physical_device_feature.pNext                        = &vulkan_1_2_features;
                vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_feature);

                if (physical_device_properties.properties.deviceType == preferred_type)
                {
                    PhysicalDevice                             = physical_device;
                    PhysicalDeviceProperties                   = physical_device_properties;
                    PhysicalDeviceVulkan12Properties           = vulkan_1_2_properties;
                    PhysicalDeviceFeature                      = physical_device_feature;
                    PhysicalDeviceSupportSampledImageBindless  = (vulkan_1_2_features.runtimeDescriptorArray == VK_TRUE && vulkan_1_2_features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE && vulkan_1_2_features.descriptorBindingPartiallyBound == VK_TRUE && vulkan_1_2_features.descriptorBindingUpdateUnusedWhilePending == VK_TRUE);
                    PhysicalDeviceSupportStorageBufferBindless = (vulkan_1_2_features.runtimeDescriptorArray == VK_TRUE && vulkan_1_2_features.descriptorBindingPartiallyBound == VK_TRUE);
                    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &PhysicalDeviceMemoryProperties);
                    PhysicalDeviceSupportTimelineSemaphore = (vulkan_1_2_features.timelineSemaphore == VK_TRUE);
                    return true;
                }
            }
            return false;
        };

        // Prefer hardware GPUs; fall back to CPU (software renderer) if none available
        if (!try_select_device(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
        {
            if (!try_select_device(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU))
            {
                try_select_device(VK_PHYSICAL_DEVICE_TYPE_CPU);
            }
        }

        Array<const char*> requested_device_enabled_layer_name_collection;
        Array<const char*> requested_device_extension_layer_name_collection;
        requested_device_enabled_layer_name_collection.init(scratch.Arena, 5);
        requested_device_extension_layer_name_collection.init(scratch.Arena, 2);

        requested_device_extension_layer_name_collection.push(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requested_device_extension_layer_name_collection.push(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
#ifdef __APPLE__
        requested_device_extension_layer_name_collection.push("VK_KHR_portability_subset");
#endif

        for (LayerProperty& layer : selected_layer_property_collection)
        {
            m_layer.GetExtensionProperties(scratch.Arena, layer, &PhysicalDevice);

            if (!layer.DeviceExtensionCollection.empty())
            {
                requested_device_enabled_layer_name_collection.push(layer.Properties.layerName);
                for (const auto& extension_property : layer.DeviceExtensionCollection)
                {
                    requested_device_extension_layer_name_collection.push(extension_property.extensionName);
                }
            }
        }

        uint32_t physical_device_queue_family_count{0};
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &physical_device_queue_family_count, nullptr);

        Array<VkQueueFamilyProperties> physical_device_queue_family_collection;
        physical_device_queue_family_collection.init(scratch.Arena, physical_device_queue_family_count, physical_device_queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &physical_device_queue_family_count, physical_device_queue_family_collection.data());

        uint32_t                queue_family_index      = 0;
        VkQueueFamilyProperties queue_family_properties = {};
        for (size_t index = 0; index < physical_device_queue_family_count; ++index)
        {
            if (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                // Ensuring presentation support
                if (Surface)
                {
                    VkBool32 present_support = false;

                    ZENGINE_VALIDATE_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, index, Surface, &present_support) == VK_SUCCESS, "Failed to get device surface support information")

                    if (present_support)
                    {
                        GraphicFamilyIndex = index;
                    }
                }

                // Usually Queue family with VK_QUEUE_GRAPHICS_BIT support transfer bit
                // So we default it for transfer family as well till we find a dedicated queue for transfer is available
                if (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_TRANSFER_BIT)
                {
                    TransferFamilyIndex = index;
                }
            }
            else if ((physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_TRANSFER_BIT) && (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 && (physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0)
            {
                TransferFamilyIndex = index;
            }
        }

        HasSeperateTransfertQueueFamily                             = GraphicFamilyIndex != TransferFamilyIndex;

        const float                    queue_prorities[]            = {1.0f};
        auto                           family_index_collection      = std::set{GraphicFamilyIndex, TransferFamilyIndex};
        Array<VkDeviceQueueCreateInfo> queue_create_info_collection = {};
        queue_create_info_collection.init(scratch.Arena, family_index_collection.size());
        for (uint32_t queue_family_index : family_index_collection)
        {
            VkDeviceQueueCreateInfo& queue_create_info = queue_create_info_collection.push_use(VkDeviceQueueCreateInfo{});
            queue_create_info.sType                    = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.pQueuePriorities         = queue_prorities;
            queue_create_info.queueFamilyIndex         = queue_family_index;
            queue_create_info.queueCount               = 1;
            queue_create_info.pNext                    = nullptr;
        }

        /*
         * Enabling some features
         */
        VkDeviceCreateInfo device_create_info                = {};
        device_create_info.sType                             = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount              = queue_create_info_collection.size();
        device_create_info.pQueueCreateInfos                 = queue_create_info_collection.data();
        device_create_info.enabledExtensionCount             = static_cast<uint32_t>(requested_device_extension_layer_name_collection.size());
        device_create_info.ppEnabledExtensionNames           = (requested_device_extension_layer_name_collection.size() > 0) ? requested_device_extension_layer_name_collection.data() : nullptr;

        VkPhysicalDeviceVulkan12Features vulkan_1_2_features = {};
        vulkan_1_2_features.sType                            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        if (PhysicalDeviceSupportTimelineSemaphore)
        {
            vulkan_1_2_features.timelineSemaphore = VK_TRUE;
        }

        VkPhysicalDeviceFeatures2 device_features_2          = {};
        device_features_2.sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        device_features_2.features.drawIndirectFirstInstance = PhysicalDeviceFeature.features.drawIndirectFirstInstance;
        device_features_2.features.multiDrawIndirect         = PhysicalDeviceFeature.features.multiDrawIndirect;
        device_features_2.features.samplerAnisotropy         = PhysicalDeviceFeature.features.samplerAnisotropy;

        if (PhysicalDeviceSupportSampledImageBindless || PhysicalDeviceSupportStorageBufferBindless)
        {
            if (PhysicalDeviceSupportSampledImageBindless)
            {
                vulkan_1_2_features.descriptorBindingUpdateUnusedWhilePending    = VK_TRUE;
                vulkan_1_2_features.shaderSampledImageArrayNonUniformIndexing    = VK_TRUE;
                vulkan_1_2_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            }

            vulkan_1_2_features.descriptorBindingPartiallyBound = VK_TRUE;
            vulkan_1_2_features.runtimeDescriptorArray          = VK_TRUE;

            device_features_2.pNext                             = &vulkan_1_2_features;
        }

        device_create_info.pNext = &device_features_2;

        ZENGINE_VALIDATE_ASSERT(vkCreateDevice(PhysicalDevice, &device_create_info, nullptr, &LogicalDevice) == VK_SUCCESS, "Failed to create GPU logical device")

        /*Create Vulkan Graphic Queue*/
        VkQueue graphic_queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(LogicalDevice, GraphicFamilyIndex, 0, &graphic_queue);
        m_queue_map.insert(Rendering::QueueType::GRAPHIC_QUEUE, std::move(graphic_queue));

        /*Create Vulkan Transfer Queue*/
        if (HasSeperateTransfertQueueFamily)
        {
            VkQueue transfer_queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(LogicalDevice, TransferFamilyIndex, 0, &transfer_queue);
            m_queue_map.insert(Rendering::QueueType::TRANSFER_QUEUE, std::move(transfer_queue));
        }

        /* Surface format selection */
        uint32_t                  format_count    = 0;
        Array<VkSurfaceFormatKHR> surface_formats = {};
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &format_count, nullptr);
        if (format_count != 0)
        {
            surface_formats.init(scratch.Arena, format_count, format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &format_count, surface_formats.data());

            for (const VkSurfaceFormatKHR& format_khr : surface_formats)
            {
                // default is: VK_FORMAT_B8G8R8A8_SRGB
                // but Imgui wants : VK_FORMAT_B8G8R8A8_UNORM ...
                if ((format_khr.format == VK_FORMAT_B8G8R8A8_UNORM) && (format_khr.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
                {
                    SurfaceFormat = format_khr;
                    break;
                }
            }
        }

        /* Present Mode selection */
        uint32_t                present_mode_count = 0;
        Array<VkPresentModeKHR> present_modes      = {};
        vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &present_mode_count, nullptr);
        if (present_mode_count != 0)
        {
            present_modes.init(scratch.Arena, present_mode_count, present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &present_mode_count, present_modes.data());

            if (window->IsVSyncEnable())
            {
                PresentMode = VK_PRESENT_MODE_FIFO_KHR;
                for (const VkPresentModeKHR present_mode_khr : present_modes)
                {
                    if (present_mode_khr == VK_PRESENT_MODE_MAILBOX_KHR)
                    {
                        PresentMode = present_mode_khr;
                        break;
                    }
                }
            }
            else
            {
                PresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
                for (const VkPresentModeKHR present_mode_khr : present_modes)
                {
                    if (present_mode_khr == VK_PRESENT_MODE_IMMEDIATE_KHR)
                    {
                        PresentMode = present_mode_khr;
                        break;
                    }
                }
            }
        }

        bool has_budget = false;
        bool has_bda    = false;
        for (uint32_t ext_i = 0; ext_i < requested_device_extension_layer_name_collection.size(); ++ext_i)
        {
            const char* ext = requested_device_extension_layer_name_collection[ext_i];
            if (strcmp(ext, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
                has_budget = true;
            if (strcmp(ext, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0)
                has_bda = true;
        }

        ZReleaseScratch(scratch);

        /*
         * Creating VMA Allocators
         */
        GpuMem.Initialize(PhysicalDevice, LogicalDevice, Instance, has_budget, has_bda);

        /*
         * Creating Swapchain
         */
        // todo(jeanphilippekernel): Should pass MaxFrameCount instead of hard-coded 3
        SwapchainPtr->Initialize(this, 3);

        /*
         * Creating Buffer Manager
         */
        CommandBufferMgr->Initialize(this, SwapchainPtr->BufferredFrameCount);

        /*
         * Creating Per-Frame Upload Heaps
         */
        for (uint32_t i = 0; i < SwapchainPtr->BufferredFrameCount; ++i)
        {
            char name[32];
            snprintf(name, sizeof(name), "FrameHeap[%u]", i);
            FrameHeaps[i].Initialize(&GpuMem, name);
        }

        /*
         * Creating Global Descriptor Pool for : Textures, Samplers
         */
        VkSamplerCreateInfo linear_sampler_create_info                   = {};
        linear_sampler_create_info.sType                                 = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        linear_sampler_create_info.minFilter                             = VK_FILTER_LINEAR;
        linear_sampler_create_info.magFilter                             = VK_FILTER_LINEAR;
        linear_sampler_create_info.addressModeU                          = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_sampler_create_info.addressModeV                          = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_sampler_create_info.addressModeW                          = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_sampler_create_info.anisotropyEnable                      = PhysicalDeviceFeature.features.samplerAnisotropy;
        linear_sampler_create_info.maxAnisotropy                         = PhysicalDeviceFeature.features.samplerAnisotropy ? PhysicalDeviceProperties.properties.limits.maxSamplerAnisotropy : 1.0f;
        linear_sampler_create_info.borderColor                           = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        linear_sampler_create_info.unnormalizedCoordinates               = VK_FALSE;
        linear_sampler_create_info.compareEnable                         = VK_FALSE;
        linear_sampler_create_info.mipmapMode                            = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        linear_sampler_create_info.mipLodBias                            = 0.0f;
        linear_sampler_create_info.minLod                                = 0.0f;
        linear_sampler_create_info.maxLod                                = VK_LOD_CLAMP_NONE;

        VkSamplerCreateInfo linear_sampler_clamp_to_edge_create_info     = {};
        linear_sampler_clamp_to_edge_create_info.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        linear_sampler_clamp_to_edge_create_info.minFilter               = VK_FILTER_LINEAR;
        linear_sampler_clamp_to_edge_create_info.magFilter               = VK_FILTER_LINEAR;
        linear_sampler_clamp_to_edge_create_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linear_sampler_clamp_to_edge_create_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linear_sampler_clamp_to_edge_create_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        linear_sampler_clamp_to_edge_create_info.anisotropyEnable        = VK_FALSE;
        linear_sampler_clamp_to_edge_create_info.maxAnisotropy           = 1.0f;
        linear_sampler_clamp_to_edge_create_info.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        linear_sampler_clamp_to_edge_create_info.unnormalizedCoordinates = VK_FALSE;
        linear_sampler_clamp_to_edge_create_info.compareEnable           = VK_FALSE;
        linear_sampler_clamp_to_edge_create_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        linear_sampler_clamp_to_edge_create_info.mipLodBias              = 0.0f;
        linear_sampler_clamp_to_edge_create_info.minLod                  = 0.0f;
        linear_sampler_clamp_to_edge_create_info.maxLod                  = VK_LOD_CLAMP_NONE;

        ZENGINE_VALIDATE_ASSERT(vkCreateSampler(LogicalDevice, &linear_sampler_create_info, nullptr, &GlobalLinearWrapSampler) == VK_SUCCESS, "Failed to create Texture Sampler")
        ZENGINE_VALIDATE_ASSERT(vkCreateSampler(LogicalDevice, &linear_sampler_clamp_to_edge_create_info, nullptr, &GlobalLinearClampToEdgeSampler) == VK_SUCCESS, "Failed to create Texture Sampler")

        GlobalLinearWrapSamplerImageInfo        = VkDescriptorImageInfo{.sampler = GlobalLinearWrapSampler, .imageView = VK_NULL_HANDLE, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        GlobalLinearClampToEdgeSamplerImageInfo = VkDescriptorImageInfo{.sampler = GlobalLinearClampToEdgeSampler, .imageView = VK_NULL_HANDLE, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        MaxGlobalTexture                        = std::min(MaxGlobalTexture, PhysicalDeviceVulkan12Properties.maxPerStageDescriptorUpdateAfterBindSampledImages - 1);

        GlobalTextures.Initialize(Arena, MaxGlobalTexture);
        Image2DBufferManager.Initialize(Arena, MaxGlobalTexture);
        {
            VkDescriptorSetLayoutCreateInfo empty_layout_info = {};
            empty_layout_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            empty_layout_info.bindingCount                    = 0;
            empty_layout_info.pBindings                       = nullptr;
            ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorSetLayout(LogicalDevice, &empty_layout_info, nullptr, &EmptyDescriptorSetLayout) == VK_SUCCESS, "Failed to create EmptyDescriptorSetLayout")

            // Allocate one descriptor set from the empty layout so gap slots in
            // per-shader DescriptorSetMaps always have a bindable (zero-binding) set.
            VkDescriptorPoolCreateInfo empty_pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            empty_pool_info.maxSets                    = 1;
            empty_pool_info.poolSizeCount              = 0;
            empty_pool_info.pPoolSizes                 = nullptr;
            ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(LogicalDevice, &empty_pool_info, nullptr, &EmptyDescriptorPoolHandle) == VK_SUCCESS, "Failed to create EmptyDescriptorPool")

            VkDescriptorSetAllocateInfo empty_alloc_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            empty_alloc_info.descriptorPool              = EmptyDescriptorPoolHandle;
            empty_alloc_info.descriptorSetCount          = 1;
            empty_alloc_info.pSetLayouts                 = &EmptyDescriptorSetLayout;
            ZENGINE_VALIDATE_ASSERT(vkAllocateDescriptorSets(LogicalDevice, &empty_alloc_info, &EmptyDescriptorSet) == VK_SUCCESS, "Failed to allocate EmptyDescriptorSet")
        }

        ShaderReservedLayoutBindingSpecificationMap.init(Arena, 1);

        ShaderReservedLayoutBindingSpecificationMap[1].init(Arena, 2);
        ShaderReservedLayoutBindingSpecificationMap[1].push(LayoutBindingSpecification{.Set = 1, .Binding = 0, .Count = MaxGlobalTexture, .Name = "TextureArray", .DescriptorTypeValue = DescriptorType::SAMPLED_IMAGE, .Flags = ShaderStageFlags::FRAGMENT});
        ShaderReservedLayoutBindingSpecificationMap[1].push(LayoutBindingSpecification{.Set = 1, .Binding = 1, .Count = 1, .Name = "LinearWrapSampler", .DescriptorTypeValue = DescriptorType::SAMPLER, .Flags = ShaderStageFlags::FRAGMENT});
        ShaderReservedLayoutBindingSpecificationMap[1].push(LayoutBindingSpecification{.Set = 1, .Binding = 2, .Count = 1, .Name = "LinearClampSampler", .DescriptorTypeValue = DescriptorType::SAMPLER, .Flags = ShaderStageFlags::FRAGMENT});

        ShaderReservedDescriptorSetMap.init(Arena, ShaderReservedLayoutBindingSpecificationMap.size());
        ShaderReservedDescriptorSetLayoutMap.init(Arena, ShaderReservedLayoutBindingSpecificationMap.size());

        VkDescriptorPoolSize pool_sizes[] = {
            {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = (MaxGlobalTexture * SwapchainPtr->BufferredFrameCount)},
            {      .type = VK_DESCRIPTOR_TYPE_SAMPLER,                .descriptorCount = (2 * SwapchainPtr->BufferredFrameCount)}
        };

        for (const auto& layout_binding_set : ShaderReservedLayoutBindingSpecificationMap)
        {
            scratch                                                = ZGetScratch(Arena);

            Array<VkDescriptorSetLayoutBinding> layout_bindings    = {};

            uint32_t                            binding_spec_count = layout_binding_set.second.size();
            layout_bindings.init(scratch.Arena, binding_spec_count, binding_spec_count);
            for (uint32_t i = 0; i < binding_spec_count; ++i)
            {
                auto& binding_spec = layout_binding_set.second[i];

                layout_bindings[i] = VkDescriptorSetLayoutBinding{.binding = binding_spec.Binding, .descriptorType = DescriptorTypeMap[VALUE_FROM_SPEC_MAP(binding_spec.DescriptorTypeValue)], .descriptorCount = binding_spec.Count, .stageFlags = ShaderStageFlagsMap[VALUE_FROM_SPEC_MAP(binding_spec.Flags)], .pImmutableSamplers = nullptr};
            }

            Array<VkDescriptorBindingFlags> binding_flags = {};
            binding_flags.init(scratch.Arena, layout_bindings.size(), layout_bindings.size());

            for (uint32_t i = 0; i < layout_bindings.size(); ++i)
            {
                binding_flags[i] = 0; // We zeroing as we iterate

                if (PhysicalDeviceSupportSampledImageBindless && ((layout_bindings[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) || (layout_bindings[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)))
                {
                    binding_flags[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
                }
                else if (PhysicalDeviceSupportStorageBufferBindless && (layout_bindings[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER))
                {
                    binding_flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                }
            }
            VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info = {};
            binding_flags_create_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            binding_flags_create_info.bindingCount                                = binding_flags.size();
            binding_flags_create_info.pBindingFlags                               = binding_flags.data();
            /*
             * Creating SetLayout
             */
            VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info     = {};
            descriptor_set_layout_create_info.sType                               = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_set_layout_create_info.bindingCount                        = layout_bindings.size();
            descriptor_set_layout_create_info.pBindings                           = layout_bindings.data();
            if (PhysicalDeviceSupportSampledImageBindless)
            {
                descriptor_set_layout_create_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
                descriptor_set_layout_create_info.pNext = &binding_flags_create_info;
            }
            VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
            ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorSetLayout(LogicalDevice, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout) == VK_SUCCESS, "Failed to create DescriptorSetLayout")

            ZReleaseScratch(scratch);

            ShaderReservedDescriptorSetLayoutMap.insert(layout_binding_set.first, descriptor_set_layout);
        }

        VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.flags                      = PhysicalDeviceSupportSampledImageBindless ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT : 0;
        pool_info.maxSets                    = SwapchainPtr->BufferredFrameCount;
        pool_info.poolSizeCount              = sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize);
        pool_info.pPoolSizes                 = pool_sizes;
        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorPool(LogicalDevice, &pool_info, nullptr, &GlobalDescriptorPoolHandle) == VK_SUCCESS, "Failed to create Global DescriptorPool")

        for (const auto& layout : ShaderReservedDescriptorSetLayoutMap)
        {
            ShaderReservedDescriptorSetMap[layout.first].init(Arena, SwapchainPtr->BufferredFrameCount, SwapchainPtr->BufferredFrameCount);
        }

        scratch = ZGetScratch(Arena);

        for (const auto& layout : ShaderReservedDescriptorSetLayoutMap)
        {
            Array<VkDescriptorSetLayout> layout_set = {};
            layout_set.init(scratch.Arena, SwapchainPtr->BufferredFrameCount, SwapchainPtr->BufferredFrameCount);
            for (uint32_t i = 0; i < SwapchainPtr->BufferredFrameCount; ++i)
            {
                layout_set[i] = layout.second;
            }

            VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {};
            descriptor_set_allocate_info.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptor_set_allocate_info.descriptorPool              = GlobalDescriptorPoolHandle;
            descriptor_set_allocate_info.descriptorSetCount          = SwapchainPtr->BufferredFrameCount;
            descriptor_set_allocate_info.pSetLayouts                 = layout_set.data();
            ZENGINE_VALIDATE_ASSERT(vkAllocateDescriptorSets(LogicalDevice, &descriptor_set_allocate_info, ShaderReservedDescriptorSetMap[layout.first].data()) == VK_SUCCESS, "Failed to create DescriptorSet")
        }

        ZReleaseScratch(scratch);
    }

    void VulkanDevice::Deinitialize()
    {
        QueueWaitAll();

        PendingFree.Drain(&GpuMem, LogicalDevice, UINT64_MAX);
        GpuMem.Ring.Drain(UINT64_MAX);

        {
            Rendering::Textures::TextureHandle tex_to_dispose = {};
            while (TextureHandleToDispose.Pop(tex_to_dispose))
            {
                auto texture = GlobalTextures.Access(tex_to_dispose);
                if (texture)
                {
                    auto buf = Image2DBufferManager.Access(texture->BufferHandle);
                    if (buf)
                    {
                        buf->Dispose();
                    }
                    Image2DBufferManager.Remove(texture->BufferHandle);
                    GlobalTextures.Remove(tex_to_dispose);
                }
            }
        }

        GlobalTextures.Dispose();
        Image2DBufferManager.Dispose();
        ShaderManager.Dispose();

        SwapchainPtr->Dispose();
        CommandBufferMgr->Deinitialize();

        for (auto set_layout : ShaderReservedDescriptorSetLayoutMap)
        {
            vkDestroyDescriptorSetLayout(LogicalDevice, reinterpret_cast<VkDescriptorSetLayout>(set_layout.second), nullptr);
        }
        ShaderReservedDescriptorSetLayoutMap.clear();

        if (EmptyDescriptorPoolHandle)
        {
            vkDestroyDescriptorPool(LogicalDevice, EmptyDescriptorPoolHandle, nullptr);
            EmptyDescriptorPoolHandle = VK_NULL_HANDLE;
            EmptyDescriptorSet        = VK_NULL_HANDLE;
        }

        if (EmptyDescriptorSetLayout)
        {
            vkDestroyDescriptorSetLayout(LogicalDevice, EmptyDescriptorSetLayout, nullptr);
            EmptyDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (GlobalDescriptorPoolHandle)
        {
            vkDestroyDescriptorPool(LogicalDevice, GlobalDescriptorPoolHandle, nullptr);
            GlobalDescriptorPoolHandle = VK_NULL_HANDLE;
        }

        for (uint32_t i = 0; i < SwapchainPtr->BufferredFrameCount; ++i)
        {
            FrameHeaps[i].Shutdown(&GpuMem);
        }

        ZENGINE_DESTROY_VULKAN_HANDLE(Instance, vkDestroySurfaceKHR, Surface, nullptr)

        // Drain deferred frees enqueued during the Dispose() calls above
        PendingFree.Drain(&GpuMem, LogicalDevice, UINT64_MAX);
    }

    void VulkanDevice::Dispose()
    {
        // Final drain: catches deferred VkHandle frees (framebuffers, render passes,
        // pipelines, etc.) queued between the last Deinitialize drain and here.
        PendingFree.Drain(&GpuMem, LogicalDevice, UINT64_MAX);

        GpuMem.Shutdown();

        if (__destroyDebugMessengerPtr)
        {
            ZENGINE_DESTROY_VULKAN_HANDLE(Instance, __destroyDebugMessengerPtr, m_debug_messenger, nullptr)
            __destroyDebugMessengerPtr = nullptr;
            __createDebugMessengerPtr  = nullptr;
        }
        vkDestroySampler(LogicalDevice, GlobalLinearWrapSampler, nullptr);
        vkDestroySampler(LogicalDevice, GlobalLinearClampToEdgeSampler, nullptr);
        vkDestroyDevice(LogicalDevice, nullptr);
        vkDestroyInstance(Instance, nullptr);

        GlobalLinearWrapSampler        = VK_NULL_HANDLE;
        GlobalLinearClampToEdgeSampler = VK_NULL_HANDLE;
        LogicalDevice                  = VK_NULL_HANDLE;
        Instance                       = VK_NULL_HANDLE;
    }

    void VulkanDevice::QueueSubmit(CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, uint32_t wait_flag, uint64_t signal_value, uint64_t wait_value, Rendering::Primitives::Semaphore* const wait_semaphore)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer->GetState() == CommandBufferState::Executable, "Command buffer must be in executable state to be submitted.")
        ZENGINE_VALIDATE_ASSERT(signal_semaphore->IsTimeline == true, "Signal semaphore must be a timeline semaphore.")

        bool                          has_wait                       = (wait_semaphore != nullptr && wait_value != UINT64_MAX);

        VkPipelineStageFlags          flag                           = VkPipelineStageFlagBits(wait_flag);

        VkCommandBuffer               command_buffers[]              = {command_buffer->GetHandle()};
        VkSemaphore                   semaphores[]                   = {signal_semaphore->GetHandle()};
        VkSemaphore                   wait_sems[]                    = {has_wait ? wait_semaphore->GetHandle() : VK_NULL_HANDLE};
        uint64_t                      wait_values[]                  = {wait_value};
        uint64_t                      signal_values[]                = {signal_value};

        VkTimelineSemaphoreSubmitInfo timeline_semaphore_submit_info = {
            .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .pNext                     = nullptr,
            .waitSemaphoreValueCount   = has_wait ? 1u : 0u,
            .pWaitSemaphoreValues      = has_wait ? wait_values : nullptr,
            .signalSemaphoreValueCount = 1,
            .pSignalSemaphoreValues    = signal_values,
        };

        VkSubmitInfo submit_info = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = &timeline_semaphore_submit_info,
            .waitSemaphoreCount   = has_wait ? 1u : 0u,
            .pWaitSemaphores      = has_wait ? wait_sems : nullptr,
            .pWaitDstStageMask    = has_wait ? &flag : nullptr,
            .commandBufferCount   = 1,
            .pCommandBuffers      = command_buffers,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = semaphores,
        };

        ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(GetQueue(command_buffer->QueueType).Handle, 1, &submit_info, VK_NULL_HANDLE) == VK_SUCCESS, "Failed to submit queue")
        command_buffer->SetState(CommandBufferState::Pending);
    }

    bool VulkanDevice::QueueSubmit(const VkPipelineStageFlags wait_stage_flag, CommandBuffer* command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, Rendering::Primitives::Fence* const fence)
    {
        ZENGINE_VALIDATE_ASSERT(fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")

        // Todo : Think of a way to signal/wait the same  semaphore signal_semaphore
        ZENGINE_VALIDATE_ASSERT(signal_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")

        VkPipelineStageFlags flags[]      = {wait_stage_flag};
        VkSemaphore          semaphores[] = {signal_semaphore->GetHandle()};
        VkCommandBuffer      buffers[]    = {command_buffer->GetHandle()};
        VkSubmitInfo         submit_info  = {
            // clang-format off
                     .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .pNext                = nullptr,
                     .waitSemaphoreCount   = 0,
                     .pWaitSemaphores      = 0,
                     .pWaitDstStageMask    = flags,
                     .commandBufferCount   = 1,
                     .pCommandBuffers      = buffers,
                     .signalSemaphoreCount = 0,
                     .pSignalSemaphores    = 0,
            //clang-format on
        };

        ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(GetQueue(command_buffer->QueueType).Handle, 1, &submit_info, fence->GetHandle()) == VK_SUCCESS, "Failed to submit queue")
        command_buffer->SetState(CommandBufferState::Pending);

        fence->SetState(FenceState::Submitted);
        signal_semaphore->SetState(SemaphoreState::Submitted);

        if (!fence->Wait())
        {
            ZENGINE_CORE_WARN("Failed to wait for Command buffer's Fence, due to timeout")
            return false;
        }

        fence->Reset();
        signal_semaphore->SetState(Rendering::Primitives::SemaphoreState::Idle);
        command_buffer->SetState(CommandBufferState::Invalid);

        return true;
    }

    void VulkanDevice::QueueWait(Rendering::QueueType type)
    {
        if (!HasSeperateTransfertQueueFamily)
        {
            type = QueueType::GRAPHIC_QUEUE;
        }
        ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(m_queue_map.at(type)) == VK_SUCCESS, "Failed to wait on queue")
    }

    QueueView VulkanDevice::GetQueue(Rendering::QueueType type)
    {
        uint32_t queue_family_index = 0;
        switch (type)
        {
            case ZEngine::Rendering::QueueType::GRAPHIC_QUEUE:
                queue_family_index = GraphicFamilyIndex;
                break;
            case ZEngine::Rendering::QueueType::TRANSFER_QUEUE:
                queue_family_index = HasSeperateTransfertQueueFamily ? TransferFamilyIndex : GraphicFamilyIndex;
                break;
        }
        return QueueView{.FamilyIndex = queue_family_index, .Handle = m_queue_map.at(type)};
    }

    void VulkanDevice::QueueWaitAll()
    {
        QueueWait(Rendering::QueueType::TRANSFER_QUEUE);
        QueueWait(Rendering::QueueType::GRAPHIC_QUEUE);
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::__debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
    {
        if ((messageSeverity & static_cast<decltype(messageSeverity)>(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            ZENGINE_CORE_ERROR("{}", pCallbackData->pMessage)
        }

        if ((messageSeverity & static_cast<decltype(messageSeverity)>(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            ZENGINE_CORE_WARN("{}", pCallbackData->pMessage)
        }

        if ((messageSeverity & static_cast<decltype(messageSeverity)>(VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        {
            ZENGINE_CORE_TRACE("{}", pCallbackData->pMessage)
        }

        if ((messageSeverity & static_cast<decltype(messageSeverity)>(VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            ZENGINE_CORE_INFO("{}", pCallbackData->pMessage)
        }

        return VK_FALSE;
    }

    void VulkanDevice::MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data)
    {
        if (data)
        {
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(GpuMem.Allocator, data, buffer.Allocation, 0, data_size) == VK_SUCCESS, "Failed to map and copy memory")
        }
    }

    BufferView VulkanDevice::CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, Core::Memory::GpuMemoryDomain domain, const char* debug_name)
    {
        BufferView buffer_view = GpuMem.AllocateBuffer(byte_size, buffer_usage, domain, debug_name);

        buffer_view.FrameIndex = SwapchainPtr->CurrentFrame == nullptr ? 0u : SwapchainPtr->CurrentFrame->Index;

        if (buffer_usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            buffer_view.Type = Core::Memory::BufferType::VERTEX;
        else if (buffer_usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
            buffer_view.Type = Core::Memory::BufferType::INDEX;
        else if (buffer_usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            buffer_view.Type = Core::Memory::BufferType::STORAGE;
        else if (buffer_usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
            buffer_view.Type = Core::Memory::BufferType::INDIRECT;
        else if (buffer_usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            buffer_view.Type = Core::Memory::BufferType::UNIFORM;

        return buffer_view;
    }

    VkPipelineStageFlags VulkanDevice::CopyBuffer(CommandBuffer* command_buffer, const BufferView& source, const BufferView& destination, VkDeviceSize byte_size, VkDeviceSize src_buffer_offset, VkDeviceSize dst_buffer_offset)
    {
        VkBufferMemoryBarrier bufMemBarrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        bufMemBarrier.srcAccessMask         = VK_ACCESS_HOST_WRITE_BIT;
        bufMemBarrier.dstAccessMask         = VK_ACCESS_TRANSFER_READ_BIT;
        bufMemBarrier.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier.buffer                = source.Handle;
        bufMemBarrier.offset                = 0;
        bufMemBarrier.size                  = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(command_buffer->GetHandle(), VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &bufMemBarrier, 0, nullptr);

        VkBufferCopy buffer_copy = {};
        buffer_copy.srcOffset    = src_buffer_offset;
        buffer_copy.dstOffset    = dst_buffer_offset;
        buffer_copy.size         = byte_size;

        vkCmdCopyBuffer(command_buffer->GetHandle(), source.Handle, destination.Handle, 1, &buffer_copy);

        VkAccessFlags        dst_access_mask    = VK_ACCESS_NONE;
        VkPipelineStageFlags dst_pipeline_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        switch (source.Type)
        {
            case BufferType::VERTEX:
                dst_access_mask    = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
                dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                break;

            case BufferType::INDEX:
                dst_access_mask    = VK_ACCESS_INDEX_READ_BIT;
                dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
                break;

            case BufferType::UNIFORM:
                dst_access_mask    = VK_ACCESS_UNIFORM_READ_BIT;
                dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
                break;

            case BufferType::STORAGE:
                dst_access_mask    = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                dst_pipeline_stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                break;

            case BufferType::INDIRECT:
                dst_access_mask    = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                dst_pipeline_stage = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
                break;
            case UNKNOWN:
                break;
        }

        VkBufferMemoryBarrier bufMemBarrier2 = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        bufMemBarrier2.srcAccessMask         = VK_ACCESS_TRANSFER_WRITE_BIT;
        bufMemBarrier2.dstAccessMask         = dst_access_mask;
        bufMemBarrier2.srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier2.dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier2.buffer                = destination.Handle;
        bufMemBarrier2.offset                = 0;
        bufMemBarrier2.size                  = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(command_buffer->GetHandle(), VK_PIPELINE_STAGE_TRANSFER_BIT, dst_pipeline_stage, 0, 0, nullptr, 1, &bufMemBarrier2, 0, nullptr);

        return dst_pipeline_stage;
    }

    BufferImage VulkanDevice::CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count, VkImageCreateFlags image_create_flag_bit)
    {
        VkImageCreateInfo image_create_info            = {};
        image_create_info.sType                        = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.flags                        = image_create_flag_bit;
        image_create_info.imageType                    = image_type;
        image_create_info.extent.width                 = width;
        image_create_info.extent.height                = height;
        image_create_info.extent.depth                 = 1;
        image_create_info.mipLevels                    = 1;
        image_create_info.arrayLayers                  = layer_count;
        image_create_info.format                       = image_format;
        image_create_info.tiling                       = image_tiling;
        image_create_info.initialLayout                = image_initial_layout;
        image_create_info.usage                        = image_usage;
        image_create_info.sharingMode                  = image_sharing_mode;
        image_create_info.samples                      = image_sample_count;

        Core::Memory::GpuMemoryDomain domain = (image_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT || image_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            ? Core::Memory::GpuMemoryDomain::RenderTarget
            : Core::Memory::GpuMemoryDomain::DeviceTexture;

        BufferImage buffer_image = GpuMem.AllocateImage(image_create_info, domain, LogicalDevice, image_aspect_flag, image_view_type, layer_count);

        // Metadata info
        buffer_image.FrameIndex = SwapchainPtr->CurrentFrame == nullptr ? 0u : SwapchainPtr->CurrentFrame->Index;

        return buffer_image;
    }

    VkFormat VulkanDevice::FindSupportedFormat(Core::Containers::ArrayView<VkFormat> format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags)
    {
        VkFormat supported_format = VK_FORMAT_UNDEFINED;
        for (uint32_t i = 0; i < format_collection.size(); ++i)
        {
            bool               found = false;
            VkFormatProperties format_properties;
            vkGetPhysicalDeviceFormatProperties(PhysicalDevice, format_collection[i], &format_properties);

            if (image_tiling == VK_IMAGE_TILING_LINEAR && (format_properties.linearTilingFeatures & feature_flags) == feature_flags)
            {
                supported_format = format_collection[i];
                found            = true;
            }
            else if (image_tiling == VK_IMAGE_TILING_OPTIMAL && (format_properties.optimalTilingFeatures & feature_flags) == feature_flags)
            {
                supported_format = format_collection[i];
                found            = true;
            }

            if (found)
            {
                break;
            }
        }

        ZENGINE_VALIDATE_ASSERT(supported_format != VK_FORMAT_UNDEFINED, "Failed to find supported Image format")

        return supported_format;
    }

    VkFormat VulkanDevice::FindDepthFormat()
    {
        return FindSupportedFormat(ArrayView<VkFormat>{DefaultDepthFormats}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
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

        ZENGINE_VALIDATE_ASSERT(vkCreateImageView(LogicalDevice, &image_view_create_info, nullptr, &image_view) == VK_SUCCESS, "Failed to create image view")

        return image_view;
    }

    VkFramebuffer VulkanDevice::CreateFramebuffer(Core::Containers::ArrayView<VkImageView> attachments, const VkRenderPass& render_pass, uint32_t width, uint32_t height, uint32_t layer_number)
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

        ZENGINE_VALIDATE_ASSERT(vkCreateFramebuffer(LogicalDevice, &framebuffer_create_info, nullptr, &framebuffer) == VK_SUCCESS, "Failed to create Framebuffer")

        return framebuffer;
    }




    Helpers::Handle<Rendering::Shaders::Shader> VulkanDevice::CompileShader(Rendering::Specifications::ShaderSpecification& spec)
    {
        if (ShaderCaches.contains(spec.Name))
        {
            return ShaderCaches.at(spec.Name);
        }

        auto handle = ShaderManager.Create();
        auto shader = ShaderManager.Access(handle);

        if (shader)
        {
            auto* vfs      = Engine::GetContext()->VFS;
            auto  try_set  = [&](const std::string& path, cstring& out) {
                auto vfs_path = Core::VFS::VFSPath::Parse(path.c_str());
                if (!vfs_path.Succeeded())
                    return;
                auto exists = vfs->Exists(vfs_path.Value());
                if (!exists.Succeeded() || !exists.Value())
                    return;
                auto n = path.size() + 1u;
                auto s = ZPushString(Arena, n);
                Helpers::secure_strcpy(s, n, path.c_str());
                out = s;
            };

            try_set(fmt::format("/ZodiacEngine/Shaders/Cache/{}_vertex.spv", spec.Name), spec.VertexFilename);
            try_set(fmt::format("/ZodiacEngine/Shaders/Cache/{}_fragment.spv", spec.Name), spec.FragmentFilename);

            shader->Initialize(this, spec);
        }
        return handle;
    }

    /*
     * CommandBufferManager impl
     */
    CommandBuffer::CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool primary) : Device(device), QueueType(type), m_command_pool(command_pool)
    {
        Device->Arena->CreateSubArena(ZKilo(120), &LocalArena);
        BufferType = primary ? CommandBufferType::Primary : CommandBufferType::Secondary;
        Create();
    }

    CommandBuffer::~CommandBuffer()
    {
        Free();
    }

    void CommandBuffer::Create()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_pool != VK_NULL_HANDLE, "Command Pool cannot be null")

        VkCommandBufferAllocateInfo command_buffer_allocation_info = {};
        command_buffer_allocation_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocation_info.level                       = (BufferType == CommandBufferType::Primary) ? VK_COMMAND_BUFFER_LEVEL_PRIMARY : VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        command_buffer_allocation_info.commandBufferCount          = 1;
        command_buffer_allocation_info.commandPool                 = m_command_pool;

        ZENGINE_VALIDATE_ASSERT(vkAllocateCommandBuffers(Device->LogicalDevice, &command_buffer_allocation_info, &m_command_buffer) == VK_SUCCESS, "Failed to allocate command buffer!")
        m_command_buffer_state = CommandBufferState::Idle;
    }

    void CommandBuffer::Free()
    {
        if (m_command_pool && m_command_buffer)
        {
            VkCommandBuffer buffers[] = {m_command_buffer};
            vkFreeCommandBuffers(Device->LogicalDevice, m_command_pool, 1, buffers);
            m_command_buffer = VK_NULL_HANDLE;
        }
    }

    VkCommandBuffer CommandBuffer::GetHandle() const
    {
        return m_command_buffer;
    }

    void CommandBuffer::Begin()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommandBufferState::Idle, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "command buffer must be Primary Buffer Type")

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(m_command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")

        m_command_buffer_state = CommandBufferState::Recording;
    }

    void CommandBuffer::BeginSecondary(Rendering::Renderers::RenderPasses::RenderPass* const render_pass, VkFramebuffer framebuffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommandBufferState::Idle, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Secondary, "command buffer must be Secondary Buffer Type")

        VkCommandBufferInheritanceInfo inheritance_info    = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        inheritance_info.renderPass                        = render_pass->GetAttachment()->GetHandle();
        inheritance_info.subpass                           = 0;
        inheritance_info.framebuffer                       = framebuffer;

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        command_buffer_begin_info.pInheritanceInfo         = &inheritance_info;

        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(m_command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")

        m_command_buffer_state = CommandBufferState::Recording;
        m_active_render_pass   = render_pass;
    }

    void CommandBuffer::End()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommandBufferState::Recording, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(m_command_buffer) == VK_SUCCESS, "Failed to end recording command buffer!")

        m_command_buffer_state = CommandBufferState::Executable;
    }

    bool CommandBuffer::Completed()
    {
        return m_signal_fence ? m_signal_fence->IsSignaled() : false;
    }

    bool CommandBuffer::IsExecutable()
    {
        return m_command_buffer_state == CommandBufferState::Executable;
    }

    bool CommandBuffer::IsRecording()
    {
        return m_command_buffer_state == CommandBufferState::Recording;
    }

    CommandBufferState CommandBuffer::GetState() const
    {
        return CommandBufferState{m_command_buffer_state.load()};
    }

    void CommandBuffer::ResetState()
    {
        m_command_buffer_state = CommandBufferState::Idle;
        m_signal_fence         = {};
        m_signal_semaphore     = {};
    }

    void CommandBuffer::SetState(const CommandBufferState& state)
    {
        m_command_buffer_state = state;
    }

    Primitives::Semaphore* CommandBuffer::GetSignalSemaphore() const
    {
        return m_signal_semaphore;
    }

    void CommandBuffer::SetSignalFence(Primitives::Fence* const semaphore)
    {
        m_signal_fence = semaphore;
    }

    void CommandBuffer::SetSignalSemaphore(Primitives::Semaphore* const semaphore)
    {
        m_signal_semaphore = semaphore;
    }

    Primitives::Fence* CommandBuffer::GetSignalFence()
    {
        return m_signal_fence;
    }

    void CommandBuffer::ClearColor(float r, float g, float b, float a)
    {
        m_clear_value[0].color = {r, g, b, a};
    }

    void CommandBuffer::ClearDepth(float depth_color, uint32_t stencil)
    {
        m_clear_value[1].depthStencil.depth   = depth_color;
        m_clear_value[1].depthStencil.stencil = stencil;
    }

    void CommandBuffer::BeginRenderPass(Rendering::Renderers::RenderPasses::RenderPass* const render_pass, VkFramebuffer framebuffer, bool is_content_secondary_command_buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "command buffer must be Primary Buffer Type")

        const auto&         render_pass_spec = render_pass->Specification;
        const uint32_t      width            = render_pass->GetRenderAreaWidth();
        const uint32_t      height           = render_pass->GetRenderAreaHeight();

        auto                scratch          = ZGetScratch(&LocalArena);

        Array<VkClearValue> clear_values     = {};
        clear_values.init(scratch.Arena, 5);
        if (render_pass_spec.SwapchainAsRenderTarget)
        {
            clear_values.push(m_clear_value[0]);
        }
        else
        {
            auto& spec = render_pass->Specification;
            for (const auto& handle : spec.Inputs)
            {
                auto texture = Device->GlobalTextures.Access(handle);
                if (texture->IsDepthTexture)
                {
                    clear_values.push(m_clear_value[1]);
                    continue;
                }
                clear_values.push(m_clear_value[0]);
            }

            for (const auto& handle : spec.ExternalOutputs)
            {
                auto texture = Device->GlobalTextures.Access(handle);

                if (texture->IsDepthTexture)
                {
                    clear_values.push(m_clear_value[1]);
                    continue;
                }
                clear_values.push(m_clear_value[0]);
            }
        }

        VkRenderPassBeginInfo render_pass_begin_info = {};
        render_pass_begin_info.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass            = render_pass->GetAttachment()->GetHandle();
        render_pass_begin_info.framebuffer           = framebuffer;
        render_pass_begin_info.renderArea.offset     = {0, 0};
        render_pass_begin_info.renderArea.extent     = VkExtent2D{width, height};
        render_pass_begin_info.clearValueCount       = clear_values.size();
        render_pass_begin_info.pClearValues          = clear_values.data();

        vkCmdBeginRenderPass(m_command_buffer, &render_pass_begin_info, is_content_secondary_command_buffer ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE);

        m_active_render_pass = render_pass;

        ZReleaseScratch(scratch);
    }

    void CommandBuffer::EndRenderPass()
    {
        if (m_active_render_pass)
        {
            ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
            vkCmdEndRenderPass(m_command_buffer);
            m_active_render_pass = nullptr;
        }
    }

    void CommandBuffer::BindDescriptorSets(uint32_t frame_index, const uint32_t* dynamic_offsets, uint32_t dynamic_offset_count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (auto render_pass = m_active_render_pass)
        {
            auto                   pipeline           = render_pass->Pipeline;
            auto                   pipeline_layout    = pipeline->Layout;
            auto                   shader             = pipeline->Shader;
            const auto&            set_layout         = shader->SetLayouts;
            const auto&            descriptor_set_map = shader->DescriptorSetMap;

            auto                   scratch            = ZGetScratch(&LocalArena);
            Array<VkDescriptorSet> frame_sets         = {};
            frame_sets.init(scratch.Arena, 5);

            for (uint32_t i = 0; i < set_layout.size(); ++i)
            {
                if (descriptor_set_map.contains(i))
                {
                    frame_sets.push(descriptor_set_map.at(i)[frame_index]);
                }
            }

            if (!frame_sets.empty())
            {
                // Count actual dynamic bindings in this shader to avoid validation errors
                uint32_t actual_dynamic_count = 0;
                for (const auto& lbs : shader->LayoutBindingSpecificationMap)
                {
                    for (uint32_t i = 0; i < lbs.second.size(); ++i)
                    {
                        if (lbs.second[i].DescriptorTypeValue == Rendering::Specifications::DescriptorType::UNIFORM_BUFFER_DYNAMIC ||
                            lbs.second[i].DescriptorTypeValue == Rendering::Specifications::DescriptorType::STORAGE_BUFFER_DYNAMIC)
                        {
                            ++actual_dynamic_count;
                        }
                    }
                }

                const uint32_t* offsets = (actual_dynamic_count > 0) ? dynamic_offsets : nullptr;
                uint32_t        count   = (actual_dynamic_count > 0) ? actual_dynamic_count : 0;
                vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, frame_sets.size(), frame_sets.data(), count, offsets);
            }
            ZReleaseScratch(scratch);
        }
    }

    void CommandBuffer::BindDescriptorSet(const VkDescriptorSet& descriptor)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(descriptor != nullptr, "DescriptorSet can't be null")
        if (auto render_pass = m_active_render_pass)
        {
            auto            pipeline_layout = render_pass->Pipeline->Layout;
            VkDescriptorSet desc_set[1]     = {descriptor};
            vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, desc_set, 0, nullptr);
        }
    }

    void CommandBuffer::BindPipeline(Rendering::Specifications::PipelineBindPoint bind_point, Rendering::Renderers::Pipelines::GraphicPipeline* const pipeline)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(pipeline != nullptr, "Pipeline can't be null")
        ZENGINE_VALIDATE_ASSERT(pipeline->Handle != VK_NULL_HANDLE, "Pipeline Handle can't be null")

        // todo : adapt value based on bind_point
        vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Handle);
    }

    void CommandBuffer::DrawIndirect(VkBuffer buffer, uint32_t offset, uint32_t draw_count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer != VK_NULL_HANDLE && draw_count > 0)
        {
            vkCmdDrawIndirect(m_command_buffer, buffer, offset, draw_count, sizeof(VkDrawIndirectCommand));
        }
    }

    void CommandBuffer::DrawIndexedIndirect(VkBuffer buffer, uint32_t offset, uint32_t count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer != VK_NULL_HANDLE && count > 0)
        {
            vkCmdDrawIndexedIndirect(m_command_buffer, buffer, offset, count, sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    void CommandBuffer::DrawIndexed(uint32_t index_count, uint32_t instanceCount, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        vkCmdDrawIndexed(m_command_buffer, index_count, instanceCount, first_index, vertex_offset, first_instance);
    }

    void CommandBuffer::Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        vkCmdDraw(m_command_buffer, vertex_count, instance_count, first_index, first_instance);
    }

    void CommandBuffer::TransitionImageLayout(const Rendering::Primitives::ImageMemoryBarrier& image_barrier)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        const auto& barrier_handle = image_barrier.GetHandle();
        const auto& barrier_spec   = image_barrier.GetSpecification();
        vkCmdPipelineBarrier(m_command_buffer, barrier_spec.SourceStageMask, barrier_spec.DestinationStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier_handle);
    }

    void CommandBuffer::CopyBufferToImage(const Hardwares::BufferView& source, Hardwares::BufferImage& destination, uint32_t width, uint32_t height, uint32_t layer_count, VkImageLayout new_layout)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        VkBufferImageCopy buffer_image_copy               = {};
        buffer_image_copy.bufferOffset                    = 0;
        buffer_image_copy.bufferRowLength                 = 0;
        buffer_image_copy.bufferImageHeight               = 0;
        buffer_image_copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        buffer_image_copy.imageSubresource.mipLevel       = 0;
        buffer_image_copy.imageSubresource.baseArrayLayer = 0;
        buffer_image_copy.imageSubresource.layerCount     = layer_count;
        buffer_image_copy.imageOffset                     = {0, 0, 0};
        buffer_image_copy.imageExtent                     = {width, height, 1};

        vkCmdCopyBufferToImage(m_command_buffer, source.Handle, destination.Handle, new_layout, 1, &buffer_image_copy);
    }



    void CommandBuffer::BindVertexBuffer(const Core::Memory::BufferView& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer.Handle)
        {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(m_command_buffer, 0, 1, &buffer.Handle, &offset);
        }
    }

    void CommandBuffer::BindIndexBuffer(const Core::Memory::BufferView& buffer, VkIndexType type)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer.Handle)
            vkCmdBindIndexBuffer(m_command_buffer, buffer.Handle, 0, type);
    }

    void CommandBuffer::SetScissor(uint32_t w, uint32_t h, int32_t x, int32_t y)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        VkRect2D scissor = {};
        scissor.offset   = {x, y};
        scissor.extent   = {w, h};
        vkCmdSetScissor(m_command_buffer, 0, 1, &scissor);
    }

    void CommandBuffer::SetViewport(uint32_t w, uint32_t h, float x, float y, float min_depth, float max_depth)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        VkViewport viewport = {};
        viewport.x          = x;
        viewport.y          = y;
        viewport.width      = (float)w;
        viewport.height     = (float)h;
        viewport.minDepth   = min_depth;
        viewport.maxDepth   = max_depth;
        vkCmdSetViewport(m_command_buffer, 0, 1, &viewport);
    }

    void CommandBuffer::PushConstants(VkShaderStageFlags stage_flags, uint32_t offset, uint32_t size, const void* data)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (auto render_pass = m_active_render_pass)
        {
            auto pipeline_layout = render_pass->Pipeline->Layout;
            vkCmdPushConstants(m_command_buffer, pipeline_layout, stage_flags, offset, size, data);
        }
    }

    void CommandBuffer::ExecuteSecondaryCommandBuffers(Core::Containers::ArrayView<CommandBuffer> buffers)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "command buffer must be Primary Buffer Type")

        if (buffers.size() == 0)
        {
            ZENGINE_CORE_WARN("No secondary buffers to execute")
            return;
        }

        Array<VkCommandBuffer> handles = {};
        auto                   scratch = ZGetScratch(Device->Arena);

        handles.init(scratch.Arena, buffers.size(), buffers.size());
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            handles[i] = buffers[i].GetHandle();
        }

        vkCmdExecuteCommands(m_command_buffer, handles.size(), handles.data());

        ZReleaseScratch(scratch);
    }

    void CommandBufferManager::Initialize(VulkanDevice* device, uint32_t image_count, uint8_t override_thread_count)
    {
        if (m_is_initialized)
        {
            ZENGINE_CORE_WARN("Attempted to call {}, but it has been already initialized", __FUNCTION__)
            return;
        }

        Device                         = device;
        TotalThreadCount               = override_thread_count > 0 ? override_thread_count : device->WorkerThreadCount;
        TotalPoolCount                 = image_count * TotalThreadCount;
        TotalCommandBufferCount        = TotalPoolCount * MaxBufferPerPool;
        TotalInstantCommandBufferCount = MaxBufferPerPool * MaxBufferPerPool * TotalPoolCount; // We want to have enough instant command buffers for each pool, so we can guarantee that there will always be an instant command buffer available for each pool when needed

        InstantGraphicsPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
        InstantGraphicsCommandBuffers.init(Device->Arena, TotalInstantCommandBufferCount, TotalInstantCommandBufferCount);
        CommandPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
        CommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
        EnqueuedCommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);

        for (uint32_t i = 0; i < TotalPoolCount; ++i)
        {
            InstantGraphicsPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, QueueType::GRAPHIC_QUEUE);

            for (uint32_t buf_idx = 0; buf_idx < (MaxBufferPerPool * MaxBufferPerPool); ++buf_idx)
            {
                uint32_t buffer_idx                       = (i * (MaxBufferPerPool * MaxBufferPerPool)) + buf_idx;
                InstantGraphicsCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, InstantGraphicsPools[i]->Handle, InstantGraphicsPools[i]->QueueType, true);
            }
        }

        for (uint32_t i = 0; i < TotalPoolCount; ++i)
        {
            CommandPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, QueueType::GRAPHIC_QUEUE);
            for (uint32_t buf_idx = 0; buf_idx < MaxBufferPerPool; ++buf_idx)
            {
                uint32_t buffer_idx        = (i * MaxBufferPerPool) + buf_idx;
                bool     is_primary        = (buffer_idx % 2) == 0;
                CommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, CommandPools[i]->Handle, CommandPools[i]->QueueType, is_primary);
            }
        }

        if (Device->HasSeperateTransfertQueueFamily)
        {
            InstantTransferPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            TransferCommandPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            TransferCommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
            InstantTransferCommandBuffers.init(Device->Arena, TotalInstantCommandBufferCount, TotalInstantCommandBufferCount);

            for (uint32_t i = 0; i < TotalPoolCount; ++i)
            {
                InstantTransferPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::TRANSFER_QUEUE);
                for (uint32_t buf_idx = 0; buf_idx < (MaxBufferPerPool * MaxBufferPerPool); ++buf_idx)
                {
                    uint32_t buffer_idx                       = (i * (MaxBufferPerPool * MaxBufferPerPool)) + buf_idx;
                    InstantTransferCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, InstantTransferPools[i]->Handle, InstantTransferPools[i]->QueueType, true);
                }
            }

            for (uint32_t i = 0; i < TotalPoolCount; ++i)
            {
                TransferCommandPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::TRANSFER_QUEUE);
                for (uint32_t buf_idx = 0; buf_idx < MaxBufferPerPool; ++buf_idx)
                {
                    uint32_t buffer_idx                = (i * MaxBufferPerPool) + buf_idx;
                    TransferCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, TransferCommandPools[i]->Handle, TransferCommandPools[i]->QueueType, true);
                }
            }
        }

        m_is_initialized = true;
    }

    void CommandBufferManager::Deinitialize()
    {
        // Explicitly destroy each CommandPool — vkDestroyCommandPool implicitly frees
        // all VkCommandBuffers allocated from it.  The Array::clear() that follows only
        // zeroes the pointer list; it never invokes C++ destructors, so skipping this
        // step leaks every VkCommandPool and VkCommandBuffer past vkDestroyDevice.
        for (uint32_t i = 0; i < InstantGraphicsPools.size(); ++i)
            InstantGraphicsPools[i]->~CommandPool();
        for (uint32_t i = 0; i < CommandPools.size(); ++i)
            CommandPools[i]->~CommandPool();
        for (uint32_t i = 0; i < TransferCommandPools.size(); ++i)
            TransferCommandPools[i]->~CommandPool();
        for (uint32_t i = 0; i < InstantTransferPools.size(); ++i)
            InstantTransferPools[i]->~CommandPool();

        InstantGraphicsPools.clear();
        InstantGraphicsCommandBuffers.clear();
        CommandBuffers.clear();
        TransferCommandBuffers.clear();
        InstantTransferCommandBuffers.clear();
        CommandPools.clear();
        TransferCommandPools.clear();
        InstantTransferPools.clear();
        EnqueuedCommandBuffers.clear();
    }

    CommandBuffer* CommandBufferManager::GetCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t buffer_per_pool_index, bool begin)
    {
        auto           buffer_index = ((frame_index * TotalThreadCount) + thread_index) * MaxBufferPerPool + buffer_per_pool_index;
        CommandBuffer* buffer       = (type == Rendering::QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandBuffers[buffer_index] : CommandBuffers[buffer_index];

        if (begin)
        {
            buffer->ResetState();
            buffer->Begin();
        }
        return buffer;
    }

    CommandBuffer* CommandBufferManager::GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint32_t buffer_per_pool_index, bool begin)
    {
        // MaxBufferPerPool * MaxBufferPerPool is the total number of instant command buffers per pool
        auto           buffer_index = ((frame_index * TotalThreadCount) + thread_index) * (MaxBufferPerPool * MaxBufferPerPool) + buffer_per_pool_index;
        CommandBuffer* buffer       = (type == Rendering::QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? InstantTransferCommandBuffers[buffer_index] : InstantGraphicsCommandBuffers[buffer_index];

        if (begin)
        {
            buffer->ResetState();
            vkResetCommandBuffer(buffer->GetHandle(), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            buffer->Begin();
        }
        return buffer;
    }

    Rendering::Pools::CommandPool* CommandBufferManager::GetCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        return (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandPools[pool_index] : CommandPools[pool_index];
    }

    Rendering::Pools::CommandPool* CommandBufferManager::GetInstantCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        return (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? InstantTransferPools[pool_index] : InstantGraphicsPools[pool_index];
    }

    void CommandBufferManager::ResetPool(uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        vkResetCommandPool(Device->LogicalDevice, CommandPools[pool_index]->Handle, 0);
        if (Device->HasSeperateTransfertQueueFamily)
        {
            vkResetCommandPool(Device->LogicalDevice, TransferCommandPools[pool_index]->Handle, 0);
        }
    }

    void CommandBufferManager::ResetEnqueuedBufferIndex()
    {
        for (int i = 0; i < EnqueuedCommandBufferIndex && i < EnqueuedCommandBuffers.size(); ++i)
        {
            if (EnqueuedCommandBuffers[i])
            {
                EnqueuedCommandBuffers[i]->SetState(CommandBufferState::Pending);
            }
        }
        EnqueuedCommandBufferIndex = 0u;
    }

    void CommandBufferManager::EndEnqueuedBuffers()
    {
        for (int i = 0; i < EnqueuedCommandBufferIndex; ++i)
        {
            EnqueuedCommandBuffers[i]->End();
        }
    }

    void CommandBufferManager::EnqueueBuffer(CommandBufferPtr const buffer)
    {
        if (EnqueuedCommandBufferIndex < EnqueuedCommandBuffers.size())
        {
            EnqueuedCommandBuffers[EnqueuedCommandBufferIndex++] = buffer;
            return;
        }
        ZENGINE_CORE_ERROR("[!] Enqueued Command Buffer overflow detected")
    }

    void CommandBufferManager::IncreaseBuffers()
    {
        // TotalPoolCount          = Device->SwapchainImageCount * TotalThreadCount;
        // TotalCommandBufferCount = TotalPoolCount * MaxBufferPerPool;

        // if (TotalCommandBufferCount > EnqueuedCommandBuffers.size())
        // {
        //     auto size = EnqueuedCommandBuffers.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         EnqueuedCommandBuffers.push(nullptr);
        //     }
        // }

        // if (TotalPoolCount > CommandPools.size())
        // {
        //     auto size = CommandPools.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         CommandPools.push(ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::GRAPHIC_QUEUE));
        //     }
        // }

        // if (TotalCommandBufferCount > CommandBuffers.size())
        // {
        //     auto size = CommandBuffers.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         int   pool_index = GetPoolFromIndex(Rendering::QueueType::GRAPHIC_QUEUE, i);
        //         auto& pool       = CommandPools[pool_index];
        //         CommandBuffers.push(ZPushStructCtorArgs(
        //             Device->Arena,
        //             CommandBuffer,
        //             Device,
        //             pool->Handle,
        //             pool->QueueType,
        //             /*(i % MaxBufferPerPool) == 0 ? false : true */ false));
        //     }
        // }

        // if (Device->HasSeperateTransfertQueueFamily)
        // {
        //     auto size = TransferCommandPools.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         TransferCommandPools.push(ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::TRANSFER_QUEUE));
        //     }

        //     size = TransferCommandBuffers.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         int   pool_index = GetPoolFromIndex(Rendering::QueueType::TRANSFER_QUEUE, i);
        //         auto& pool       = TransferCommandPools[pool_index];
        //         TransferCommandBuffers.push(ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, pool->Handle, pool->QueueType, true));
        //     }
        // }
    }

    bool CommandBufferManager::IsInitialized() const
    {
        return m_is_initialized;
    }




    void Image2DBuffer::Construct(Hardwares::VulkanDevice* device)
    {
        Device = device;
        Layout = Rendering::Specifications::ImageLayout::UNDEFINED;
        ZENGINE_VALIDATE_ASSERT(Specification.Width > 0, "Image width must be greater then zero")
        ZENGINE_VALIDATE_ASSERT(Specification.Height > 0, "Image height must be greater then zero")

        Specifications::ImageViewType   image_view_type   = Specifications::ImageViewType::TYPE_2D;
        Specifications::ImageCreateFlag image_create_flag = Specifications::ImageCreateFlag::NONE;

        if (Specification.BufferUsageType == Specifications::ImageBufferUsageType::CUBEMAP)
        {
            image_view_type   = Specifications::ImageViewType::TYPE_CUBE;
            image_create_flag = Specifications::ImageCreateFlag::CUBE_COMPATIBLE_BIT;
        }

        m_buffer_image = Device->CreateImage(Specification.Width, Specification.Height, VK_IMAGE_TYPE_2D, Specifications::ImageViewTypeMap[VALUE_FROM_SPEC_MAP(image_view_type)], Specification.ImageFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED, Specification.ImageUsage, VK_SHARING_MODE_EXCLUSIVE, VK_SAMPLE_COUNT_1_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Specification.ImageAspectFlag, Specification.LayerCount, Specifications::ImageCreateFlagMap[VALUE_FROM_SPEC_MAP(image_create_flag)]);
    }

    Image2DBuffer::~Image2DBuffer()
    {
        Dispose();
    }

    BufferImage& Image2DBuffer::GetBuffer()
    {
        return m_buffer_image;
    }

    const BufferImage& Image2DBuffer::GetBuffer() const
    {
        return m_buffer_image;
    }

    VkImage Image2DBuffer::GetHandle() const
    {
        return m_buffer_image.Handle;
    }

    VkSampler Image2DBuffer::GetSampler() const
    {
        return m_buffer_image.Sampler;
    }

    void Image2DBuffer::Dispose()
    {
        if (m_buffer_image)
        {
            DeferredFreeEntry e  = {};
            e.EntryKind          = DeferredFreeEntry::Kind::Image;
            e.Data.Image         = m_buffer_image;
            Device->DeferFree(e);
            m_buffer_image = {};
        }
    }

    VkDescriptorImageInfo& Image2DBuffer::GetDescriptorImageInfo()
    {
        m_image_info.sampler     = m_buffer_image.Sampler;
        m_image_info.imageView   = m_buffer_image.ViewHandle;
        m_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return m_image_info;
    }

    VkImageView Image2DBuffer::GetImageViewHandle() const
    {
        return m_buffer_image.ViewHandle;
    }











    Rendering::Textures::TextureHandle VulkanDevice::CreateTexture(uint32_t width, uint32_t height)
    {
        return CreateTexture(width, height, 255, 255, 255, 255);
    }

    Rendering::Textures::TextureHandle VulkanDevice::CreateTexture(uint32_t width, uint32_t height, float r, float g, float b, float a)
    {
        uint32_t                             byte_per_pixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(Specifications::ImageFormat::R8G8B8A8_SRGB)];

        Specifications::TextureSpecification spec           = {
          // clang-format off
                      .Width        = width,
                      .Height       = height,
                      .BytePerPixel = byte_per_pixel,
                      .Format       = Specifications::ImageFormat::R8G8B8A8_SRGB,
            // clang-format on
        };

        auto tex_handle = CreateTexture(spec);

        if (!tex_handle)
        {
            return Rendering::Textures::TextureHandle{};
        }

        auto                 scratch    = ZGetScratch(Arena);

        size_t               data_size  = width * height * byte_per_pixel;
        Array<unsigned char> image_data = {};
        image_data.init(scratch.Arena, data_size, data_size);

        unsigned char r_byte = static_cast<unsigned char>(std::clamp(r * 255.0f, 0.0f, 255.0f));
        unsigned char g_byte = static_cast<unsigned char>(std::clamp(g * 255.0f, 0.0f, 255.0f));
        unsigned char b_byte = static_cast<unsigned char>(std::clamp(b * 255.0f, 0.0f, 255.0f));
        unsigned char a_byte = static_cast<unsigned char>(std::clamp(a * 255.0f, 0.0f, 255.0f));

        for (size_t i = 0; i < data_size; i += byte_per_pixel)
        {
            image_data[i]     = r_byte;
            image_data[i + 1] = g_byte;
            image_data[i + 2] = b_byte;
            image_data[i + 3] = a_byte;
        }

        if (RRM)
            static_cast<Rendering::RenderResourceManager*>(RRM)->UploadTextureBuffer(0, 0, tex_handle, image_data.data());
        ZReleaseScratch(scratch);

        return tex_handle;
    }

    Rendering::Textures::TextureHandle VulkanDevice::CreateTexture(const Rendering::Specifications::TextureSpecification& spec)
    {
        std::unique_lock l(Mutex);

        auto             handle = GlobalTextures.Create();

        if (!handle)
        {
            return Rendering::Textures::TextureHandle{};
        }

        auto resource = GlobalTextures.Access(handle);

        if (!resource)
        {
            return Rendering::Textures::TextureHandle{};
        }

        resource->Specification              = spec;
        resource->Width                      = spec.Width;
        resource->Height                     = spec.Height;
        resource->BytePerPixel               = spec.BytePerPixel;
        resource->BufferSize                 = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
        resource->IsDepthTexture             = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);

        uint32_t storage_bit                 = spec.IsUsageStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
        uint32_t transfert_bit               = spec.IsUsageTransfert ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
        uint32_t sampled_bit                 = spec.IsUsageSampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
        uint32_t image_aspect                = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t image_usage_attachment      = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkFormat image_format                = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? FindDepthFormat() : Specifications::ImageFormatMap[VALUE_FROM_SPEC_MAP(spec.Format)];

        auto     buff_handle                 = Image2DBufferManager.Create();
        auto     buffer_res                  = Image2DBufferManager.Access(buff_handle);

        buffer_res->Specification            = {.Width = spec.Width, .Height = spec.Height, .BufferUsageType = spec.IsCubemap ? Specifications::ImageBufferUsageType::CUBEMAP : Specifications::ImageBufferUsageType::SINGLE_2D_IMAGE, .ImageFormat = image_format, .ImageAspectFlag = VkImageAspectFlagBits(image_aspect), .LayerCount = spec.LayerCount};
        buffer_res->Specification.ImageUsage = VkImageUsageFlagBits(image_usage_attachment | transfert_bit | sampled_bit | storage_bit);
        buffer_res->Construct(this);

        resource->BufferHandle = buff_handle;

        return handle;
    }

    BufferView VulkanDevice::WriteTextureData(CommandBufferPtr command_buf, const Rendering::Textures::TextureHandle& handle, const void* data)
    {
        if (!handle.Valid() || !(data) || !(command_buf))
        {
            return {};
        }

        auto     resource    = GlobalTextures.Access(handle);
        auto     image_buf   = Image2DBufferManager.Access(resource->BufferHandle);

        uint32_t ring_offset = 0;
        void*    ring_ptr    = GpuMem.Ring.Allocate(static_cast<uint32_t>(resource->BufferSize), 4, &ring_offset);

        if (ring_ptr)
        {
            Helpers::secure_memcpy(ring_ptr, resource->BufferSize, data, resource->BufferSize);
            BufferView ring_view = {};
            ring_view.Handle     = GpuMem.Ring.Buffer;
            ring_view.Allocation = GpuMem.Ring.Allocation;
            command_buf->CopyBufferToImage(ring_view, image_buf->GetBuffer(), resource->Width, resource->Height, resource->Specification.LayerCount, Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(image_buf->Layout)]);
            // Return empty view — ring owns lifetime, caller must not free
            return {};
        }

        BufferView staging_view = CreateBuffer(resource->BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Core::Memory::GpuMemoryDomain::HostStaging, "WriteTextureData_staging");
        MapAndCopyToMemory(staging_view, resource->BufferSize, data);
        command_buf->CopyBufferToImage(staging_view, image_buf->GetBuffer(), resource->Width, resource->Height, resource->Specification.LayerCount, Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(image_buf->Layout)]);
        return staging_view;
    }

    Rendering::Renderers::RenderPasses::RenderPass* VulkanDevice::CreateRenderPass(const Rendering::Specifications::RenderPassSpecification& spec)
    {
        auto pass = ZPushStructCtorArgs(Arena, Rendering::Renderers::RenderPasses::RenderPass);
        pass->Initialize(this, spec);
        return pass;
    }

    void VulkanDevice::TickMemory()
    {
        uint64_t completed = 0;
        vkGetSemaphoreCounterValue(LogicalDevice, SwapchainPtr->RenderTimeline->GetHandle(), &completed);
        PendingFree.Drain(&GpuMem, LogicalDevice, completed);
        GpuMem.Ring.Drain(completed);
        GpuMem.SampleBudgets();

        uint32_t fi = SwapchainPtr->CurrentFrame == nullptr ? 0u : SwapchainPtr->CurrentFrame->Index;
        FrameHeaps[fi].Reset();
    }

    uint32_t VulkanDevice::MinUniformBufferOffsetAlignment() const
    {
        return static_cast<uint32_t>(PhysicalDeviceProperties.properties.limits.minUniformBufferOffsetAlignment);
    }

    uint32_t VulkanDevice::MinStorageBufferOffsetAlignment() const
    {
        return static_cast<uint32_t>(PhysicalDeviceProperties.properties.limits.minStorageBufferOffsetAlignment);
    }

    void VulkanDevice::DeferFree(DeferredFreeEntry entry)
    {
        entry.TimelineValue = SwapchainPtr->RenderTimelineNextValue;
        PendingFree.Enqueue(entry);
    }

    void VulkanDevice::EnqueueAsyncGPUOperation(const AsyncGPUOperationHandle& operation)
    {
        AsyncGPUOperations.Enqueue(operation);
    }

} // namespace ZEngine::Hardwares
