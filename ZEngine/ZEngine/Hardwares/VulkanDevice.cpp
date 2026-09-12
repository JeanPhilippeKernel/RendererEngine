#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Hardwares/CommandBufferManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Pools/CommandPool.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <cstdlib>
#include <filesystem>
#include <limits>

using namespace std::chrono_literals;
using namespace ZEngine::Rendering::Primitives;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Hardwares
{
    namespace
    {
        template <typename T>
        uint64_t ToDebugObjectHandle(T handle)
        {
#if VK_USE_64_BIT_PTR_DEFINES == 1
            return reinterpret_cast<uint64_t>(handle);
#else
            return static_cast<uint64_t>(handle);
#endif
        }
    } // namespace

    void AsyncGPUOperation::Initialize(VulkanDevice* device, uint32_t total_buffer_count)
    {
        NextValue = 0;
        Timeline  = ZPushStructCtorArgs(device->Arena, Semaphore, device, true);
        RetireValues.init(device->Arena, total_buffer_count, total_buffer_count);
    }

    void VulkanDevice::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::CoreWindow* const window, uint32_t worker_thread_count)
    {
        Arena             = arena;
        CurrentWindow     = window;
        WorkerThreadCount = worker_thread_count;
        BindlessTextureSlotRequests.init(Arena, 64);
        ShaderReservedBindingSets.init(Arena, 4);
        ShaderReservedBindingSets.insert(1);
        CommandBufferMgr = ZPushStructCtor(Arena, CommandBufferManager);
        SwapchainPtr     = ZPushStructCtor(Arena, DeviceSwapchain);

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
        auto                  scratch = ZGetScratch(Arena);

        Array<const char*>    enabled_layer_name_collection;
        Array<LayerProperty*> selected_layer_property_collection;
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
            auto find_it = std::find_if(std::begin(m_layer.InstanceLayers), std::end(m_layer.InstanceLayers), [&](const LayerProperty& layer_property) { return Helpers::secure_strcmp(layer_property.Properties.layerName, layer_name) == 0; });
            if (find_it == std::end(m_layer.InstanceLayers))
            {
                continue;
            }

            enabled_layer_name_collection.push(find_it->Properties.layerName);
            selected_layer_property_collection.push(find_it);
        }
#endif

        Array<const char*> enabled_extension_layer_name_collection;
        enabled_extension_layer_name_collection.init(scratch.Arena, 16);

#ifdef ENABLE_VULKAN_VALIDATION_LAYER
        bool validation_features_available = false;
#endif

        for (LayerProperty* const layer : selected_layer_property_collection)
        {
            for (const auto& extension : layer->ExtensionCollection)
            {
                if (Helpers::secure_strcmp(extension.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0)
                {
#ifdef ENABLE_VULKAN_VALIDATION_LAYER
                    validation_features_available = true;
#endif
                    continue;
                }
                if (Helpers::secure_strcmp(extension.extensionName, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME) == 0)
                    continue;
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

        // GPU debug labels are useful independently of validation layers (for
        // RenderDoc, Xcode GPU Frame Capture, and RenderDoc on Linux/Windows).
        // Enable the instance extension only when the loader advertises it.
        uint32_t global_extension_count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &global_extension_count, nullptr) == VK_SUCCESS && global_extension_count > 0)
        {
            Array<VkExtensionProperties> global_extensions;
            global_extensions.init(scratch.Arena, global_extension_count, global_extension_count);
            if (vkEnumerateInstanceExtensionProperties(nullptr, &global_extension_count, global_extensions.data()) == VK_SUCCESS)
            {
                bool debug_utils_available = false;
                for (const auto& extension : global_extensions)
                {
                    if (Helpers::secure_strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                    {
                        debug_utils_available = true;
                        break;
                    }
                }

                bool debug_utils_enabled = false;
                for (const char* extension : enabled_extension_layer_name_collection)
                {
                    if (Helpers::secure_strcmp(extension, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                    {
                        debug_utils_enabled = true;
                        break;
                    }
                }
                if (debug_utils_available && !debug_utils_enabled)
                    enabled_extension_layer_name_collection.push(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
        }

#ifdef ENABLE_VULKAN_VALIDATION_LAYER
        VkValidationFeatureEnableEXT synchronization_validation_feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT      validation_features                = {
            .sType                          = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .pNext                          = nullptr,
            .enabledValidationFeatureCount  = 0,
            .pEnabledValidationFeatures     = nullptr,
            .disabledValidationFeatureCount = 0,
            .pDisabledValidationFeatures    = nullptr,
        };
        if (validation_features_available)
        {
            enabled_extension_layer_name_collection.push(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            validation_features.enabledValidationFeatureCount = 1;
            validation_features.pEnabledValidationFeatures    = &synchronization_validation_feature;
            instance_create_info.pNext                        = &validation_features;
        }
        else if (!enabled_layer_name_collection.empty())
        {
            ZENGINE_CORE_WARN("[GPU] VK_EXT_validation_features is unavailable; synchronization validation is disabled")
        }
#endif

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
            .pNext        = instance_create_info.pNext,
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
                VkPhysicalDeviceDriverProperties driver_props            = {};
                driver_props.sType                                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

                VkPhysicalDeviceVulkan12Properties vulkan_1_2_properties = {};
                vulkan_1_2_properties.sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
                vulkan_1_2_properties.pNext                              = &driver_props;

                VkPhysicalDeviceProperties2 physical_device_properties   = {};
                physical_device_properties.sType                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                physical_device_properties.pNext                         = &vulkan_1_2_properties;

                vkGetPhysicalDeviceProperties2(physical_device, &physical_device_properties);

                // VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS: fences/semaphores signal before
                // GPU execution completes — no reliable Vulkan workaround. Halt early.
                if (driver_props.driverID == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS)
                {
                    ZENGINE_CORE_CRITICAL(
                        "[GPU] Unsupported Vulkan driver detected: Intel HD/UHD Graphics (Windows proprietary). "
                        "This driver has known Vulkan synchronization bugs that cause GPU device loss. "
                        "Please update to the latest Intel graphics driver from https://www.intel.com/content/www/us/en/download-center/home.html "
                        "or wait for the DirectX 12 backend.")
                    ZENGINE_VALIDATE_ASSERT(false, "Intel HD/UHD Windows proprietary Vulkan driver is not supported — see engine log.")
                }

                VkPhysicalDeviceVulkan12Features vulkan_1_2_features                           = {};
                vulkan_1_2_features.sType                                                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

                VkPhysicalDeviceVulkan13Features vulkan_1_3_features                           = {};
                vulkan_1_3_features.sType                                                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
                vulkan_1_2_features.pNext                                                      = &vulkan_1_3_features;

                VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional_rendering_features = {};
                conditional_rendering_features.sType                                           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT;
                vulkan_1_3_features.pNext                                                      = &conditional_rendering_features;

                VkPhysicalDeviceFeatures2 physical_device_feature                              = {};
                physical_device_feature.sType                                                  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
                physical_device_feature.pNext                                                  = &vulkan_1_2_features;
                vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_feature);

                if (physical_device_properties.properties.deviceType != preferred_type)
                    continue;

                // vkQueueSubmit2, deferred retirement, render-graph queue edges,
                // and asynchronous uploads all use this baseline. Selecting a device
                // without it would create invalid timeline semaphores later.
                if (physical_device_properties.properties.apiVersion < VK_API_VERSION_1_3 || vulkan_1_2_features.timelineSemaphore != VK_TRUE || vulkan_1_3_features.synchronization2 != VK_TRUE)
                {
                    ZENGINE_CORE_WARN("[GPU] Skipping '{}' because Vulkan 1.3, timeline semaphores, and Synchronization2 are required", physical_device_properties.properties.deviceName)
                    continue;
                }

                uint32_t candidate_queue_family_count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &candidate_queue_family_count, nullptr);
                Array<VkQueueFamilyProperties> candidate_queue_families = {};
                candidate_queue_families.init(scratch.Arena, candidate_queue_family_count, candidate_queue_family_count);
                vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &candidate_queue_family_count, candidate_queue_families.data());

                bool has_present_graphics_queue = false;
                for (uint32_t queue_index = 0; queue_index < candidate_queue_family_count; ++queue_index)
                {
                    if ((candidate_queue_families[queue_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
                        continue;
                    VkBool32 present_support = VK_FALSE;
                    if (vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_index, Surface, &present_support) == VK_SUCCESS && present_support == VK_TRUE)
                    {
                        has_present_graphics_queue = true;
                        break;
                    }
                }
                if (!has_present_graphics_queue)
                {
                    ZENGINE_CORE_WARN("[GPU] Skipping '{}' because no graphics queue supports the active presentation surface", physical_device_properties.properties.deviceName)
                    continue;
                }

                PhysicalDevice                             = physical_device;
                PhysicalDeviceProperties                   = physical_device_properties;
                PhysicalDeviceVulkan12Properties           = vulkan_1_2_properties;
                PhysicalDeviceFeature                      = physical_device_feature;
                PhysicalDeviceSupportSampledImageBindless  = (vulkan_1_2_features.runtimeDescriptorArray == VK_TRUE && vulkan_1_2_features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE && vulkan_1_2_features.descriptorBindingPartiallyBound == VK_TRUE && vulkan_1_2_features.descriptorBindingUpdateUnusedWhilePending == VK_TRUE);
                PhysicalDeviceSupportStorageBufferBindless = (vulkan_1_2_features.runtimeDescriptorArray == VK_TRUE && vulkan_1_2_features.descriptorBindingPartiallyBound == VK_TRUE);
                vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &PhysicalDeviceMemoryProperties);
                PhysicalDeviceSupportTimelineSemaphore    = true;
                PhysicalDeviceSupportDynamicRendering     = vulkan_1_3_features.dynamicRendering == VK_TRUE;
                PhysicalDeviceSupportHostQueryReset       = vulkan_1_2_features.hostQueryReset == VK_TRUE;
                PhysicalDeviceSupportConditionalRendering = conditional_rendering_features.conditionalRendering == VK_TRUE;
                return true;
            }
            return false;
        };

        // Prefer hardware GPUs; fall back to CPU (software renderer) if none available
        bool selected_device = try_select_device(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        if (!selected_device)
        {
            selected_device = try_select_device(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
            if (!selected_device)
            {
                selected_device = try_select_device(VK_PHYSICAL_DEVICE_TYPE_CPU);
            }
        }
        ZENGINE_VALIDATE_ASSERT(selected_device, "No Vulkan 1.3 device with timeline semaphores and Synchronization2 is available")

        Array<const char*> requested_device_enabled_layer_name_collection;
        Array<const char*> requested_device_extension_layer_name_collection;
        requested_device_enabled_layer_name_collection.init(scratch.Arena, 5);
        requested_device_extension_layer_name_collection.init(scratch.Arena, 6);

        requested_device_extension_layer_name_collection.push(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requested_device_extension_layer_name_collection.push(VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME);
#ifdef __APPLE__
        requested_device_extension_layer_name_collection.push("VK_KHR_portability_subset");
#endif

        // Conditional rendering remains optional. Query the selected device rather
        // than assuming the feature struct implies extension availability.
        uint32_t device_extension_count = 0;
        if (PhysicalDeviceSupportConditionalRendering && vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &device_extension_count, nullptr) == VK_SUCCESS && device_extension_count > 0)
        {
            Array<VkExtensionProperties> device_extensions = {};
            device_extensions.init(scratch.Arena, device_extension_count, device_extension_count);
            if (vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &device_extension_count, device_extensions.data()) == VK_SUCCESS)
            {
                bool extension_available = false;
                for (const VkExtensionProperties& extension : device_extensions)
                {
                    if (Helpers::secure_strcmp(extension.extensionName, VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME) == 0)
                    {
                        extension_available = true;
                        break;
                    }
                }
                if (extension_available)
                    requested_device_extension_layer_name_collection.push(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME);
                else
                    PhysicalDeviceSupportConditionalRendering = false;
            }
            else
            {
                PhysicalDeviceSupportConditionalRendering = false;
            }
        }
        else
        {
            PhysicalDeviceSupportConditionalRendering = false;
        }

        for (LayerProperty* const layer : selected_layer_property_collection)
        {
            m_layer.GetExtensionProperties(scratch.Arena, *layer, &PhysicalDevice);

            if (!layer->DeviceExtensionCollection.empty())
            {
                requested_device_enabled_layer_name_collection.push(layer->Properties.layerName);
                for (const auto& extension_property : layer->DeviceExtensionCollection)
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

        constexpr uint32_t invalid_queue_family               = std::numeric_limits<uint32_t>::max();
        uint32_t           dedicated_transfer_family_index    = invalid_queue_family;
        uint32_t           non_graphics_transfer_family_index = invalid_queue_family;

        for (uint32_t index = 0; index < physical_device_queue_family_count; ++index)
        {
            const VkQueueFlags flags             = physical_device_queue_family_collection[index].queueFlags;
            const bool         supports_graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
            const bool         supports_compute  = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
            const bool         supports_transfer = (flags & VK_QUEUE_TRANSFER_BIT) != 0;

            // Retain the first graphics family that can present. The selected family
            // anchors all fallbacks, so replacing it later can accidentally undo a
            // better transfer or compute choice made earlier in this loop.
            if (supports_graphics && GraphicFamilyIndex == invalid_queue_family)
            {
                if (Surface == VK_NULL_HANDLE)
                {
                    GraphicFamilyIndex = index;
                }
                else
                {
                    VkBool32 present_support = VK_FALSE;
                    ZENGINE_VALIDATE_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR(PhysicalDevice, index, Surface, &present_support) == VK_SUCCESS, "Failed to get device surface support information")
                    if (present_support == VK_TRUE)
                        GraphicFamilyIndex = index;
                }
            }

            // A non-graphics compute family can overlap graph compute work with the
            // graphics queue. Keep the first such family; Vulkan queue-family order
            // does not encode a quality ranking beyond its capabilities.
            if (supports_compute && !supports_graphics && ComputeFamilyIndex == invalid_queue_family)
                ComputeFamilyIndex = index;

            // Transfer selection is deliberately ranked: transfer-only, then any
            // non-graphics transfer family, then the graphics fallback below. Do not
            // overwrite a stronger candidate when a later graphics family is seen.
            if (supports_transfer && !supports_graphics && !supports_compute && dedicated_transfer_family_index == invalid_queue_family)
            {
                dedicated_transfer_family_index = index;
            }
            else if (supports_transfer && !supports_graphics && non_graphics_transfer_family_index == invalid_queue_family)
            {
                non_graphics_transfer_family_index = index;
            }
        }

        ZENGINE_VALIDATE_ASSERT(GraphicFamilyIndex != invalid_queue_family, "No graphics queue family supports the active presentation surface")

        if (dedicated_transfer_family_index != invalid_queue_family)
            TransferFamilyIndex = dedicated_transfer_family_index;
        else if (non_graphics_transfer_family_index != invalid_queue_family)
            TransferFamilyIndex = non_graphics_transfer_family_index;
        else
            TransferFamilyIndex = GraphicFamilyIndex;

        // Graphics queues support compute operations, making it the correct fallback
        // on devices that expose one universal queue only.
        if (ComputeFamilyIndex == invalid_queue_family)
            ComputeFamilyIndex = GraphicFamilyIndex;

        HasSeperateTransfertQueueFamily          = GraphicFamilyIndex != TransferFamilyIndex;
        HasSeparateComputeQueueFamily            = GraphicFamilyIndex != ComputeFamilyIndex;
        HasSeparateTransferQueue                 = HasSeperateTransfertQueueFamily;
        HasSeparateComputeQueue                  = HasSeparateComputeQueueFamily;
        GraphicQueueIndex                        = 0;
        TransferQueueIndex                       = 0;
        ComputeQueueIndex                        = 0;

        // A family can expose multiple distinct queue handles. Prefer a dedicated
        // family, then consume additional graphics-family queues for compute and
        // transfer work without introducing a queue-family ownership transfer.
        uint32_t       next_graphics_queue_index = 1;
        const uint32_t graphics_queue_count      = physical_device_queue_family_collection[GraphicFamilyIndex].queueCount;
        if (!HasSeparateComputeQueue && ComputeFamilyIndex == GraphicFamilyIndex && next_graphics_queue_index < graphics_queue_count)
        {
            ComputeQueueIndex       = next_graphics_queue_index++;
            HasSeparateComputeQueue = true;
        }
        if (!HasSeparateTransferQueue && TransferFamilyIndex == GraphicFamilyIndex && next_graphics_queue_index < graphics_queue_count)
        {
            TransferQueueIndex       = next_graphics_queue_index++;
            HasSeparateTransferQueue = true;
        }

        // Some devices expose one non-graphics family for both compute and transfer.
        // Request separate handles when that family has enough queues; otherwise both
        // roles intentionally serialize on its single queue.
        if (ComputeFamilyIndex == TransferFamilyIndex && ComputeFamilyIndex != GraphicFamilyIndex && physical_device_queue_family_collection[ComputeFamilyIndex].queueCount > 1)
        {
            ComputeQueueIndex  = 0;
            TransferQueueIndex = 1;
        }

        QueueTimestampValidBits[static_cast<uint32_t>(QueueType::GRAPHIC_QUEUE)]  = physical_device_queue_family_collection[GraphicFamilyIndex].timestampValidBits;
        QueueTimestampValidBits[static_cast<uint32_t>(QueueType::TRANSFER_QUEUE)] = physical_device_queue_family_collection[TransferFamilyIndex].timestampValidBits;
        QueueTimestampValidBits[static_cast<uint32_t>(QueueType::COMPUTE_QUEUE)]  = PhysicalDeviceProperties.properties.limits.timestampComputeAndGraphics == VK_TRUE ? physical_device_queue_family_collection[ComputeFamilyIndex].timestampValidBits : 0;

        // A device that cannot timestamp both graphics and compute does not offer
        // a portable clock domain for the graph's asynchronous queue batches.
        // Keep graphics timings, but do not emit transfer timestamps either.
        if (PhysicalDeviceProperties.properties.limits.timestampComputeAndGraphics != VK_TRUE)
            QueueTimestampValidBits[static_cast<uint32_t>(QueueType::TRANSFER_QUEUE)] = 0;

        const float     queue_priorities[]         = {1.0f, 1.0f, 1.0f};
        const uint32_t  requested_queue_families[] = {GraphicFamilyIndex, TransferFamilyIndex, ComputeFamilyIndex};
        const uint32_t  requested_queue_indices[]  = {GraphicQueueIndex, TransferQueueIndex, ComputeQueueIndex};
        Array<uint32_t> family_index_collection    = {};
        Array<uint32_t> family_queue_counts        = {};
        family_index_collection.init(scratch.Arena, 3);
        family_queue_counts.init(scratch.Arena, 3);
        for (uint32_t request_index = 0; request_index < 3; ++request_index)
        {
            const uint32_t candidate         = requested_queue_families[request_index];
            const uint32_t requested_count   = requested_queue_indices[request_index] + 1;
            bool           already_requested = false;
            for (uint32_t existing_index = 0; existing_index < family_index_collection.size(); ++existing_index)
            {
                if (family_index_collection[existing_index] == candidate)
                {
                    family_queue_counts[existing_index] = std::max(family_queue_counts[existing_index], requested_count);
                    already_requested                   = true;
                    break;
                }
            }
            if (!already_requested)
            {
                family_index_collection.push(candidate);
                family_queue_counts.push(requested_count);
            }
        }
        Array<VkDeviceQueueCreateInfo> queue_create_info_collection = {};
        queue_create_info_collection.init(scratch.Arena, family_index_collection.size());
        for (uint32_t request_index = 0; request_index < family_index_collection.size(); ++request_index)
        {
            VkDeviceQueueCreateInfo& queue_create_info = queue_create_info_collection.push_use(VkDeviceQueueCreateInfo{});
            queue_create_info.sType                    = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.pQueuePriorities         = queue_priorities;
            queue_create_info.queueFamilyIndex         = family_index_collection[request_index];
            queue_create_info.queueCount               = family_queue_counts[request_index];
            queue_create_info.pNext                    = nullptr;
        }

        /*
         * Enabling some features
         */
        VkDeviceCreateInfo device_create_info                                          = {};
        device_create_info.sType                                                       = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount                                        = queue_create_info_collection.size();
        device_create_info.pQueueCreateInfos                                           = queue_create_info_collection.data();
        device_create_info.enabledExtensionCount                                       = static_cast<uint32_t>(requested_device_extension_layer_name_collection.size());
        device_create_info.ppEnabledExtensionNames                                     = (requested_device_extension_layer_name_collection.size() > 0) ? requested_device_extension_layer_name_collection.data() : nullptr;

        VkPhysicalDeviceVulkan12Features vulkan_1_2_features                           = {};
        vulkan_1_2_features.sType                                                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkan_1_2_features.timelineSemaphore                                          = VK_TRUE;
        vulkan_1_2_features.hostQueryReset                                             = PhysicalDeviceSupportHostQueryReset ? VK_TRUE : VK_FALSE;

        VkPhysicalDeviceVulkan13Features vulkan_1_3_features                           = {};
        vulkan_1_3_features.sType                                                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan_1_3_features.synchronization2                                           = VK_TRUE;
        vulkan_1_3_features.dynamicRendering                                           = PhysicalDeviceSupportDynamicRendering ? VK_TRUE : VK_FALSE;
        vulkan_1_3_features.pNext                                                      = &vulkan_1_2_features;

        VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional_rendering_features = {};
        conditional_rendering_features.sType                                           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT;
        conditional_rendering_features.conditionalRendering                            = PhysicalDeviceSupportConditionalRendering ? VK_TRUE : VK_FALSE;
        vulkan_1_2_features.pNext                                                      = &conditional_rendering_features;

        VkPhysicalDeviceFeatures2 device_features_2                                    = {};
        device_features_2.sType                                                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        device_features_2.features.drawIndirectFirstInstance                           = PhysicalDeviceFeature.features.drawIndirectFirstInstance;
        device_features_2.features.multiDrawIndirect                                   = PhysicalDeviceFeature.features.multiDrawIndirect;
        device_features_2.features.samplerAnisotropy                                   = PhysicalDeviceFeature.features.samplerAnisotropy;
        // Required for MaterialData.AlbedoMap / NormalMap etc. (uint64_t handles in g_buffer.frag)
        // shaderInt64 no longer required — material map indices use uint32 in shader and CPU struct.

        device_features_2.pNext                                                        = &vulkan_1_3_features;

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
        }

        device_create_info.pNext = &device_features_2;

        ZENGINE_VALIDATE_ASSERT(vkCreateDevice(PhysicalDevice, &device_create_info, nullptr, &LogicalDevice) == VK_SUCCESS, "Failed to create GPU logical device")

        // Debug labels are optional tooling. Loading the commands after device
        // creation lets callers use one portable no-op wrapper on all platforms.
        __beginDebugLabelPtr           = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(LogicalDevice, "vkCmdBeginDebugUtilsLabelEXT"));
        __endDebugLabelPtr             = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(LogicalDevice, "vkCmdEndDebugUtilsLabelEXT"));
        __setDebugObjectNamePtr        = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(LogicalDevice, "vkSetDebugUtilsObjectNameEXT"));
        __beginRenderingPtr            = reinterpret_cast<PFN_vkCmdBeginRendering>(vkGetDeviceProcAddr(LogicalDevice, "vkCmdBeginRendering"));
        __endRenderingPtr              = reinterpret_cast<PFN_vkCmdEndRendering>(vkGetDeviceProcAddr(LogicalDevice, "vkCmdEndRendering"));
        __beginConditionalRenderingPtr = reinterpret_cast<PFN_vkCmdBeginConditionalRenderingEXT>(vkGetDeviceProcAddr(LogicalDevice, "vkCmdBeginConditionalRenderingEXT"));
        __endConditionalRenderingPtr   = reinterpret_cast<PFN_vkCmdEndConditionalRenderingEXT>(vkGetDeviceProcAddr(LogicalDevice, "vkCmdEndConditionalRenderingEXT"));
        if (PhysicalDeviceSupportDynamicRendering && (!__beginRenderingPtr || !__endRenderingPtr))
        {
            ZENGINE_CORE_WARN("Dynamic rendering was advertised but command entry points are unavailable; using legacy render passes")
            PhysicalDeviceSupportDynamicRendering = false;
        }
        if (PhysicalDeviceSupportConditionalRendering && (!__beginConditionalRenderingPtr || !__endConditionalRenderingPtr))
        {
            ZENGINE_CORE_WARN("Conditional rendering was advertised but command entry points are unavailable; using explicit pass fallbacks")
            PhysicalDeviceSupportConditionalRendering = false;
        }

        /*Create Vulkan Graphic Queue*/
        VkQueue graphic_queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(LogicalDevice, GraphicFamilyIndex, GraphicQueueIndex, &graphic_queue);
        m_queue_map.insert(Rendering::QueueType::GRAPHIC_QUEUE, std::move(graphic_queue));

        /*Create Vulkan Transfer Queue*/
        if (HasSeparateTransferQueue)
        {
            VkQueue transfer_queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(LogicalDevice, TransferFamilyIndex, TransferQueueIndex, &transfer_queue);
            m_queue_map.insert(Rendering::QueueType::TRANSFER_QUEUE, std::move(transfer_queue));
        }

        if (HasSeparateComputeQueue)
        {
            VkQueue compute_queue = VK_NULL_HANDLE;
            vkGetDeviceQueue(LogicalDevice, ComputeFamilyIndex, ComputeQueueIndex, &compute_queue);
            m_queue_map.insert(Rendering::QueueType::COMPUTE_QUEUE, std::move(compute_queue));
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

        PipelineStateCache = ZPushStructCtor(Arena, Rendering::Renderers::Pipelines::PSOCache);
        PipelineStateCache->Initialize(this);

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

        GlobalLinearWrapSampler                                          = PipelineStateCache->GetOrCreateSampler(linear_sampler_create_info);
        GlobalLinearClampToEdgeSampler                                   = PipelineStateCache->GetOrCreateSampler(linear_sampler_clamp_to_edge_create_info);

        GlobalLinearWrapSamplerImageInfo                                 = VkDescriptorImageInfo{.sampler = GlobalLinearWrapSampler, .imageView = VK_NULL_HANDLE, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        GlobalLinearClampToEdgeSamplerImageInfo                          = VkDescriptorImageInfo{.sampler = GlobalLinearClampToEdgeSampler, .imageView = VK_NULL_HANDLE, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        MaxGlobalTexture                                                 = std::min(MaxGlobalTexture, PhysicalDeviceVulkan12Properties.maxPerStageDescriptorUpdateAfterBindSampledImages - 1);

        GlobalTextures.Initialize(Arena, MaxGlobalTexture);
        ImageBufferManager.Initialize(Arena, MaxGlobalTexture);
        {
            VkDescriptorSetLayoutCreateInfo empty_layout_info = {};
            empty_layout_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            empty_layout_info.bindingCount                    = 0;
            empty_layout_info.pBindings                       = nullptr;
            ZENGINE_VALIDATE_ASSERT(PipelineStateCache != nullptr, "Empty descriptor layout requires the PSO cache")
            EmptyDescriptorSetLayout                   = PipelineStateCache->GetOrCreateDescriptorSetLayout(empty_layout_info);

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
            ZENGINE_VALIDATE_ASSERT(PipelineStateCache != nullptr, "Reserved descriptor layouts require the PSO cache")
            VkDescriptorSetLayout descriptor_set_layout = PipelineStateCache->GetOrCreateDescriptorSetLayout(descriptor_set_layout_create_info);

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

        if (PipelineStateCache)
            PipelineStateCache->StopAsyncPipelineCompilation();

        PendingFree.Drain(&GpuMem, LogicalDevice, UINT64_MAX);
        GpuMem.Ring.Drain(UINT64_MAX);

        {
            // Full shutdown — drain unconditionally, no timeline gate needed (mirrors
            // PendingFree.Drain(..., UINT64_MAX) above).
            TextureDisposeEntry entry = {};
            while (TextureHandleToDispose.pop(entry))
            {
                auto texture = GlobalTextures.Access(entry.Handle);
                if (texture)
                {
                    auto buf = ImageBufferManager.Access(texture->BufferHandle);
                    if (buf)
                    {
                        buf->Dispose();
                    }
                    ImageBufferManager.Remove(texture->BufferHandle);
                    GlobalTextures.Remove(entry.Handle);
                }
            }
        }

        GlobalTextures.Dispose();
        ImageBufferManager.Dispose();
        ShaderManager.Dispose();

        SwapchainPtr->Dispose();
        CommandBufferMgr->Deinitialize();

        // Reserved descriptor-set layouts are borrowed from PSOCache.
        ShaderReservedDescriptorSetLayoutMap.clear();

        if (EmptyDescriptorPoolHandle)
        {
            vkDestroyDescriptorPool(LogicalDevice, EmptyDescriptorPoolHandle, nullptr);
            EmptyDescriptorPoolHandle = VK_NULL_HANDLE;
            EmptyDescriptorSet        = VK_NULL_HANDLE;
        }

        // The empty descriptor-set layout is borrowed from PSOCache.
        EmptyDescriptorSetLayout = VK_NULL_HANDLE;

        if (GlobalDescriptorPoolHandle)
        {
            vkDestroyDescriptorPool(LogicalDevice, GlobalDescriptorPoolHandle, nullptr);
            GlobalDescriptorPoolHandle = VK_NULL_HANDLE;
        }

        if (PipelineStateCache)
        {
            // Shader disposal may have queued per-shader descriptor pools. Drain them
            // while their cached layouts are still alive, then release the cache.
            PendingFree.Drain(&GpuMem, LogicalDevice, UINT64_MAX);
            PipelineStateCache->Shutdown();
            PipelineStateCache = nullptr;
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
        if (PipelineStateCache)
        {
            PipelineStateCache->Shutdown();
            PipelineStateCache = nullptr;
        }
        vkDestroyDevice(LogicalDevice, nullptr);
        vkDestroyInstance(Instance, nullptr);

        GlobalLinearWrapSampler        = VK_NULL_HANDLE;
        GlobalLinearClampToEdgeSampler = VK_NULL_HANDLE;
        LogicalDevice                  = VK_NULL_HANDLE;
        Instance                       = VK_NULL_HANDLE;
    }

    bool VulkanDevice::QueueSubmit(CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, VkPipelineStageFlags2 wait_flag, uint64_t signal_value, uint64_t wait_value, Rendering::Primitives::Semaphore* const wait_semaphore)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer->GetState() == CommandBufferState::Executable, "Command buffer must be in executable state to be submitted.")
        ZENGINE_VALIDATE_ASSERT(signal_semaphore->IsTimeline == true, "Signal semaphore must be a timeline semaphore.")

        bool                      has_wait = (wait_semaphore != nullptr && wait_value != UINT64_MAX);

        VkCommandBufferSubmitInfo cmd_info = {
            .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = command_buffer->GetHandle(),
        };
        VkSemaphoreSubmitInfo signal_info = {
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = signal_semaphore->GetHandle(),
            .value     = signal_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        VkSemaphoreSubmitInfo wait_info = {};
        if (has_wait)
        {
            wait_info = {
                .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = wait_semaphore->GetHandle(),
                .value     = wait_value,
                .stageMask = (VkPipelineStageFlags2) wait_flag,
            };
        }
        VkSubmitInfo2 submit_info = {
            .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount   = has_wait ? 1u : 0u,
            .pWaitSemaphoreInfos      = has_wait ? &wait_info : nullptr,
            .commandBufferInfoCount   = 1,
            .pCommandBufferInfos      = &cmd_info,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos    = &signal_info,
        };

        VkResult submit_result = vkQueueSubmit2(GetQueue(command_buffer->QueueType).Handle, 1, &submit_info, VK_NULL_HANDLE);
        if (CheckDeviceLost(submit_result, "QueueSubmit (timeline)"))
            return false;
        ZENGINE_VALIDATE_ASSERT(submit_result == VK_SUCCESS, "Failed to submit queue")
        command_buffer->SetState(CommandBufferState::Pending);
        return true;
    }

    bool VulkanDevice::QueueSubmit(CommandBuffer* const command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, uint64_t signal_value, const VkSemaphoreSubmitInfo* wait_infos, uint32_t wait_info_count)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer->GetState() == CommandBufferState::Executable, "Command buffer must be in executable state to be submitted.")
        ZENGINE_VALIDATE_ASSERT(signal_semaphore && signal_semaphore->IsTimeline, "Signal semaphore must be a timeline semaphore.")

        VkCommandBufferSubmitInfo command_info  = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = command_buffer->GetHandle()};
        VkSemaphoreSubmitInfo     signal_info   = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = signal_semaphore->GetHandle(), .value = signal_value, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT};
        VkSubmitInfo2             submit_info   = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = wait_info_count, .pWaitSemaphoreInfos = wait_infos, .commandBufferInfoCount = 1, .pCommandBufferInfos = &command_info, .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &signal_info};
        VkResult                  submit_result = vkQueueSubmit2(GetQueue(command_buffer->QueueType).Handle, 1, &submit_info, VK_NULL_HANDLE);
        if (CheckDeviceLost(submit_result, "QueueSubmit (timeline waits)"))
            return false;
        ZENGINE_VALIDATE_ASSERT(submit_result == VK_SUCCESS, "Failed to submit queue")
        command_buffer->SetState(CommandBufferState::Pending);
        return true;
    }

    bool VulkanDevice::QueueSubmit(const VkPipelineStageFlags wait_stage_flag, CommandBuffer* command_buffer, Rendering::Primitives::Semaphore* const signal_semaphore, Rendering::Primitives::Fence* const fence)
    {
        if (fence)
        {
            ZENGINE_VALIDATE_ASSERT(fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")
        }

        // Todo : Think of a way to signal/wait the same  semaphore signal_semaphore
        if (signal_semaphore)
        {
            ZENGINE_VALIDATE_ASSERT(signal_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")
        }

        VkPipelineStageFlags flags[]      = {wait_stage_flag};
        VkSemaphore          semaphores[] = {signal_semaphore ? signal_semaphore->GetHandle() : VK_NULL_HANDLE};
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
                     .signalSemaphoreCount = signal_semaphore ? 1u : 0u,
                     .pSignalSemaphores    = signal_semaphore ? semaphores : nullptr,
            //clang-format on
        };

        VkResult submit_result = vkQueueSubmit(GetQueue(command_buffer->QueueType).Handle, 1, &submit_info, fence ? fence->GetHandle() : VK_NULL_HANDLE);
        if (CheckDeviceLost(submit_result, "QueueSubmit (fence)"))
            return false;
        ZENGINE_VALIDATE_ASSERT(submit_result == VK_SUCCESS, "Failed to submit queue")
        command_buffer->SetState(CommandBufferState::Pending);

        if (fence)
        {
            fence->SetState(FenceState::Submitted);
        }
        if (signal_semaphore)
        {
            signal_semaphore->SetState(SemaphoreState::Submitted);
        }

        if (fence)
        {
            if (!fence->Wait())
            {
                ZENGINE_CORE_WARN("Failed to wait for Command buffer's Fence, due to timeout")
                return false;
            }
            fence->Reset();
        }

        if (signal_semaphore)
        {
            signal_semaphore->SetState(Rendering::Primitives::SemaphoreState::Idle);
        }
        command_buffer->SetState(CommandBufferState::Invalid);

        return true;
    }

    void VulkanDevice::QueueWait(Rendering::QueueType type)
    {
        ZENGINE_VALIDATE_ASSERT(type != QueueType::COUNT, "QueueType::COUNT is not a Vulkan queue")
        if (type == QueueType::TRANSFER_QUEUE && !HasSeparateTransferQueue)
        {
            type = QueueType::GRAPHIC_QUEUE;
        }
        if (type == QueueType::COMPUTE_QUEUE && !HasSeparateComputeQueue)
            type = QueueType::GRAPHIC_QUEUE;
        ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(m_queue_map.at(type)) == VK_SUCCESS, "Failed to wait on queue")
    }

    QueueView VulkanDevice::GetQueue(Rendering::QueueType type)
    {
        ZENGINE_VALIDATE_ASSERT(type != QueueType::COUNT, "QueueType::COUNT is not a Vulkan queue")
        // Dedicated queue map entries may belong to the graphics family. Fall back
        // only when the device did not expose a distinct queue handle.
        if (type == QueueType::TRANSFER_QUEUE && !HasSeparateTransferQueue)
        {
            type = QueueType::GRAPHIC_QUEUE;
        }
        if (type == QueueType::COMPUTE_QUEUE && !HasSeparateComputeQueue)
            type = QueueType::GRAPHIC_QUEUE;

        uint32_t queue_family_index = 0;
        switch (type)
        {
            case ZEngine::Rendering::QueueType::GRAPHIC_QUEUE:
                queue_family_index = GraphicFamilyIndex;
                break;
            case ZEngine::Rendering::QueueType::TRANSFER_QUEUE:
                queue_family_index = TransferFamilyIndex;
                break;
            case ZEngine::Rendering::QueueType::COMPUTE_QUEUE:
                queue_family_index = ComputeFamilyIndex;
                break;
            case ZEngine::Rendering::QueueType::COUNT:
                break;
        }
        return QueueView{.FamilyIndex = queue_family_index, .Handle = m_queue_map.at(type)};
    }

    void VulkanDevice::QueueWaitAll()
    {
        QueueWait(Rendering::QueueType::TRANSFER_QUEUE);
        QueueWait(Rendering::QueueType::COMPUTE_QUEUE);
        QueueWait(Rendering::QueueType::GRAPHIC_QUEUE);
    }

    void VulkanDevice::BeginDebugLabel(VkCommandBuffer command_buffer, cstring name) const
    {
        // The messenger is created only when VK_EXT_debug_utils was enabled for
        // this instance. Do not call an extension command merely because a
        // loader exposed its function pointer.
        if (m_debug_messenger == VK_NULL_HANDLE || !__beginDebugLabelPtr || command_buffer == VK_NULL_HANDLE)
            return;

        VkDebugUtilsLabelEXT label = {};
        label.sType                 = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName            = name ? name : "RenderGraph pass";
        label.color[0]              = 0.20f;
        label.color[1]              = 0.55f;
        label.color[2]              = 0.95f;
        label.color[3]              = 1.00f;
        __beginDebugLabelPtr(command_buffer, &label);
    }

    void VulkanDevice::EndDebugLabel(VkCommandBuffer command_buffer) const
    {
        if (m_debug_messenger != VK_NULL_HANDLE && __endDebugLabelPtr && command_buffer != VK_NULL_HANDLE)
            __endDebugLabelPtr(command_buffer);
    }

    void VulkanDevice::SetDebugObjectName(VkObjectType object_type, uint64_t object_handle, cstring name) const
    {
        if (m_debug_messenger == VK_NULL_HANDLE || !__setDebugObjectNamePtr || object_handle == 0 || !name)
            return;

        VkDebugUtilsObjectNameInfoEXT object_name = {};
        object_name.sType                         = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        object_name.objectType                    = object_type;
        object_name.objectHandle                  = object_handle;
        object_name.pObjectName                   = name;
        __setDebugObjectNamePtr(LogicalDevice, &object_name);
    }

    bool VulkanDevice::CheckDeviceLost(VkResult result, const char* where)
    {
        if (result != VK_ERROR_DEVICE_LOST)
            return false;

        if (!IsDeviceLost.exchange(true, std::memory_order_acq_rel))
        {
            ZENGINE_CORE_CRITICAL("[GPU] VK_ERROR_DEVICE_LOST detected in {} — halting further Vulkan submission", where)
        }
        return true;
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

        SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, ToDebugObjectHandle(buffer_view.Handle), debug_name);

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

    BufferView VulkanDevice::CreateAliasingBuffer(const BufferView& backing, VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, const char* debug_name)
    {
        BufferView buffer_view = GpuMem.AllocateAliasingBuffer(backing, byte_size, buffer_usage, debug_name);
        SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, ToDebugObjectHandle(buffer_view.Handle), debug_name);
        buffer_view.FrameIndex = SwapchainPtr->CurrentFrame == nullptr ? 0u : SwapchainPtr->CurrentFrame->Index;
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

    BufferImage VulkanDevice::CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count, uint32_t mip_level_count, VkImageCreateFlags image_create_flag_bit, cstring debug_name)
    {
        VkImageCreateInfo image_create_info            = {};
        image_create_info.sType                        = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.flags                        = image_create_flag_bit;
        image_create_info.imageType                    = image_type;
        image_create_info.extent.width                 = width;
        image_create_info.extent.height                = height;
        image_create_info.extent.depth                 = 1;
        image_create_info.mipLevels                    = mip_level_count;
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

        BufferImage buffer_image = GpuMem.AllocateImage(image_create_info, domain, LogicalDevice, image_aspect_flag, image_view_type, layer_count, debug_name);
        SetDebugObjectName(VK_OBJECT_TYPE_IMAGE, ToDebugObjectHandle(buffer_image.Handle), debug_name);

        // Metadata info
        buffer_image.FrameIndex = SwapchainPtr->CurrentFrame == nullptr ? 0u : SwapchainPtr->CurrentFrame->Index;

        return buffer_image;
    }

    BufferImage VulkanDevice::CreateAliasingImage(const BufferImage& backing, uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count, uint32_t mip_level_count, VkImageCreateFlags image_create_flag_bit, cstring debug_name)
    {
        VkImageCreateInfo image_create_info = {};
        image_create_info.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_create_info.flags             = image_create_flag_bit;
        image_create_info.imageType         = image_type;
        image_create_info.extent.width      = width;
        image_create_info.extent.height     = height;
        image_create_info.extent.depth      = 1;
        image_create_info.mipLevels         = mip_level_count;
        image_create_info.arrayLayers       = layer_count;
        image_create_info.format            = image_format;
        image_create_info.tiling            = image_tiling;
        image_create_info.initialLayout     = image_initial_layout;
        image_create_info.usage             = image_usage;
        image_create_info.sharingMode       = image_sharing_mode;
        image_create_info.samples           = image_sample_count;

        BufferImage alias = GpuMem.AllocateAliasingImage(backing, image_create_info, LogicalDevice, image_aspect_flag, image_view_type, layer_count, debug_name);
        if (alias)
        {
            SetDebugObjectName(VK_OBJECT_TYPE_IMAGE, ToDebugObjectHandle(alias.Handle), debug_name);
            alias.FrameIndex = SwapchainPtr->CurrentFrame == nullptr ? 0u : SwapchainPtr->CurrentFrame->Index;
        }
        return alias;
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
        const VkImageSubresourceRange range = {
            .aspectMask     = image_aspect_flag,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = layer_count,
        };
        return CreateImageView(image, image_format, image_view_type, range);
    }

    VkImageView VulkanDevice::CreateImageView(VkImage image, VkFormat image_format, VkImageViewType image_view_type, const VkImageSubresourceRange& range)
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
        image_view_create_info.subresourceRange               = range;

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
            auto  try_set  = [&](cstring path, cstring& out) {
                auto vfs_path = Core::VFS::VFSPath::Parse(path);
                if (!vfs_path.Succeeded())
                    return;
                auto exists = vfs->Exists(vfs_path.Value());
                if (!exists.Succeeded() || !exists.Value())
                    return;
                auto n = Helpers::secure_strlen(path) + 1u;
                auto s = ZPushString(Arena, n);
                Helpers::secure_strcpy(s, n, path);
                out = s;
            };

            char path[kMaxShaderReloadPathBytes] = {};
            auto try_set_stage = [&](cstring suffix, cstring& out) {
                const int length = snprintf(path, sizeof(path), "/ZodiacEngine/Shaders/Cache/%s%s.spv", spec.Name, suffix);
                if (length > 0 && static_cast<size_t>(length) < sizeof(path))
                    try_set(path, out);
            };

            if (spec.Name)
            {
                try_set_stage("_vertex", spec.VertexFilename);
                try_set_stage("_fragment", spec.FragmentFilename);
                try_set_stage("_compute", spec.ComputeFilename);
            }

            shader->Initialize(this, spec);
            ShaderCaches.insert(spec.Name, handle);
        }
        return handle;
    }

    bool VulkanDevice::ReloadShader(cstring shader_name)
    {
        if (!shader_name || !ShaderCaches.contains(shader_name))
            return false;

        Rendering::Shaders::Shader* shader = ShaderManager.Access(ShaderCaches.at(shader_name));
        if (!shader)
            return false;

        shader->Reload();
        return true;
    }

    void VulkanDevice::RequestShaderReload(cstring shader_path)
    {
        if (!shader_path)
            return;

        const size_t length = Helpers::secure_strlen(shader_path);
        if (length == 0 || length >= kMaxShaderReloadPathBytes)
        {
            ZENGINE_CORE_WARN("Ignoring shader reload request with an invalid path")
            return;
        }

        ShaderReloadRequest request = {};
        Helpers::secure_strcpy(request.Path, sizeof(request.Path), shader_path);
        if (!ShaderReloadRequests.push(request))
            ZENGINE_CORE_WARN("Shader reload request queue is full")
    }

    void VulkanDevice::FlushShaderReloadRequests()
    {
        constexpr uint32_t kMaxReloadedShadersPerFlush = 64;
        Helpers::Handle<Rendering::Shaders::Shader> reloaded[kMaxReloadedShadersPerFlush] = {};
        uint32_t                                     reloaded_count                         = 0;
        ShaderReloadRequest                          request                                = {};
        while (ShaderReloadRequests.pop(request))
        {
            for (const auto& [shader_name, handle] : ShaderCaches)
            {
                (void) shader_name;
                Rendering::Shaders::Shader* shader = ShaderManager.Access(handle);
                if (!shader)
                    continue;

                const auto& specification = shader->m_specification;
                const bool matches = Helpers::secure_strcmp(request.Path, specification.VertexFilename) == 0 ||
                                     Helpers::secure_strcmp(request.Path, specification.FragmentFilename) == 0 ||
                                     Helpers::secure_strcmp(request.Path, specification.ComputeFilename) == 0;
                if (!matches)
                    continue;

                bool already_reloaded = false;
                for (uint32_t i = 0; i < reloaded_count; ++i)
                {
                    if (reloaded[i].Index == handle.Index && reloaded[i].Generation == handle.Generation)
                    {
                        already_reloaded = true;
                        break;
                    }
                }
                if (already_reloaded)
                    continue;

                if (reloaded_count == kMaxReloadedShadersPerFlush)
                {
                    shader->Reload();
                    continue;
                }
                reloaded[reloaded_count++] = handle;
            }
        }

        for (uint32_t i = 0; i < reloaded_count; ++i)
        {
            Rendering::Shaders::Shader* shader = ShaderManager.Access(reloaded[i]);
            if (shader)
                shader->Reload();
        }
    }

    /*
     * CommandBufferManager impl
     */
    CommandBuffer::CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool primary, Core::Memory::ArenaAllocator* parent_arena) : Device(device), QueueType(type), m_command_pool(command_pool)
    {
        Core::Memory::ArenaAllocator* const arena = parent_arena ? parent_arena : Device->Arena;
        arena->CreateSubArena(ZKilo(120), &LocalArena);
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

    void CommandBuffer::BeginSecondary(Rendering::Renderers::RenderPasses::GraphicPass* const render_pass, VkFramebuffer framebuffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommandBufferState::Idle, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Secondary, "command buffer must be Secondary Buffer Type")
        ZENGINE_VALIDATE_ASSERT(render_pass != nullptr, "Secondary command buffer render pass can't be null")

        VkCommandBufferInheritanceInfo inheritance_info    = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        VkCommandBufferInheritanceRenderingInfo rendering_inheritance = {};
        auto scratch = ZGetScratch(&LocalArena);

        if (Device->PhysicalDeviceSupportDynamicRendering)
        {
            auto* const attachment = render_pass->GetAttachment();
            ZENGINE_VALIDATE_ASSERT(attachment != nullptr, "Dynamic secondary command buffer attachment can't be null")

            Array<VkFormat> color_formats = {};
            color_formats.init(scratch.Arena, attachment->GetColorAttachmentCount());
            rendering_inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
            rendering_inheritance.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            rendering_inheritance.colorAttachmentCount = attachment->GetDynamicRenderingFormats(color_formats.data(), static_cast<uint32_t>(color_formats.capacity()), &rendering_inheritance.depthAttachmentFormat, &rendering_inheritance.stencilAttachmentFormat);
            rendering_inheritance.pColorAttachmentFormats = color_formats.data();
            inheritance_info.pNext       = &rendering_inheritance;
            inheritance_info.renderPass  = VK_NULL_HANDLE;
            inheritance_info.subpass     = 0;
            inheritance_info.framebuffer = VK_NULL_HANDLE;
        }
        else
        {
            inheritance_info.renderPass  = render_pass->GetAttachment()->GetHandle();
            inheritance_info.subpass     = 0;
            inheritance_info.framebuffer = framebuffer;
        }

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        command_buffer_begin_info.pInheritanceInfo         = &inheritance_info;

        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(m_command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")

        ZReleaseScratch(scratch);
        m_command_buffer_state = CommandBufferState::Recording;
        m_in_render_pass       = true;
    }

    void CommandBuffer::BeginSecondaryCompute()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommandBufferState::Idle, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Secondary, "command buffer must be Secondary Buffer Type")

        VkCommandBufferInheritanceInfo inheritance_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        VkCommandBufferBeginInfo        begin_info       = {};
        begin_info.sType                                 = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags                                 = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        begin_info.pInheritanceInfo                      = &inheritance_info;
        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(m_command_buffer, &begin_info) == VK_SUCCESS, "Failed to begin the secondary compute command buffer")

        m_command_buffer_state = CommandBufferState::Recording;
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
        m_active_pipeline               = nullptr;
        m_in_render_pass                = false;
        m_in_dynamic_rendering          = false;
        m_dynamic_swapchain_rendering   = false;
        m_in_conditional_rendering      = false;
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

    static VkClearValue GetTextureClearValue(const Textures::Texture& texture)
    {
        VkClearValue clear_value = {};
        if (texture.IsDepthTexture)
        {
            clear_value.depthStencil.depth   = texture.Specification.ClearDepth;
            clear_value.depthStencil.stencil = texture.Specification.ClearStencil;
        }
        else
        {
            clear_value.color.float32[0] = texture.Specification.ClearColor[0];
            clear_value.color.float32[1] = texture.Specification.ClearColor[1];
            clear_value.color.float32[2] = texture.Specification.ClearColor[2];
            clear_value.color.float32[3] = texture.Specification.ClearColor[3];
        }
        return clear_value;
    }

    void CommandBuffer::BeginDynamicRendering(const VkRenderingInfo& rendering_info)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Dynamic rendering command buffer is null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "Dynamic rendering must begin on a primary command buffer")
        ZENGINE_VALIDATE_ASSERT(!m_in_render_pass, "A render pass or dynamic-rendering instance is already active")
        ZENGINE_VALIDATE_ASSERT(Device->PhysicalDeviceSupportDynamicRendering && Device->__beginRenderingPtr != nullptr, "Dynamic rendering is unavailable")

        Device->__beginRenderingPtr(m_command_buffer, &rendering_info);
        m_in_render_pass       = true;
        m_in_dynamic_rendering = true;
    }

    void CommandBuffer::EndDynamicRendering()
    {
        if (!m_in_dynamic_rendering)
            return;

        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Dynamic rendering command buffer is null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "Dynamic rendering must end on a primary command buffer")
        ZENGINE_VALIDATE_ASSERT(Device->PhysicalDeviceSupportDynamicRendering && Device->__endRenderingPtr != nullptr, "Dynamic rendering is unavailable")

        Device->__endRenderingPtr(m_command_buffer);
        m_active_pipeline      = nullptr;
        m_in_render_pass       = false;
        m_in_dynamic_rendering = false;
    }

    void CommandBuffer::TransitionSwapchainImageToColorAttachment()
    {
        if (!Device->PhysicalDeviceSupportDynamicRendering)
            return;

        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Swapchain transition command buffer is null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "Swapchain transition must use a primary command buffer")
        ZENGINE_VALIDATE_ASSERT(!m_in_render_pass, "Swapchain transition cannot run inside rendering")

        auto* const swapchain = Device->SwapchainPtr;
        if (!swapchain || !swapchain->CurrentFrame)
            return;

        const uint32_t image_index = swapchain->CurrentFrame->ImageIndex;
        ZENGINE_VALIDATE_ASSERT(image_index < swapchain->SwapchainImages.size() && image_index < swapchain->SwapchainImageLayouts.size(), "Dynamic swapchain image index is invalid")
        if (image_index >= swapchain->SwapchainImages.size() || image_index >= swapchain->SwapchainImageLayouts.size())
            return;

        VkImageLayout& layout = swapchain->SwapchainImageLayouts[image_index];
        if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            return;

        VkImageMemoryBarrier2 transition = {};
        transition.sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        transition.srcStageMask           = layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        transition.srcAccessMask          = layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        transition.dstStageMask           = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        transition.dstAccessMask          = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        transition.oldLayout              = layout;
        transition.newLayout              = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        transition.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        transition.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        transition.image                  = swapchain->SwapchainImages[image_index];
        transition.subresourceRange       = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};

        VkDependencyInfo dependency       = {};
        dependency.sType                  = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers   = &transition;
        PipelineBarrier2(dependency);
        layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    void CommandBuffer::TransitionSwapchainImageToPresent()
    {
        if (!Device->PhysicalDeviceSupportDynamicRendering)
            return;

        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Swapchain transition command buffer is null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "Swapchain transition must use a primary command buffer")
        ZENGINE_VALIDATE_ASSERT(!m_in_render_pass, "Swapchain transition cannot run inside rendering")

        auto* const swapchain = Device->SwapchainPtr;
        if (!swapchain || !swapchain->CurrentFrame)
            return;

        const uint32_t image_index = swapchain->CurrentFrame->ImageIndex;
        ZENGINE_VALIDATE_ASSERT(image_index < swapchain->SwapchainImages.size() && image_index < swapchain->SwapchainImageLayouts.size(), "Dynamic swapchain image index is invalid")
        if (image_index >= swapchain->SwapchainImages.size() || image_index >= swapchain->SwapchainImageLayouts.size())
            return;

        VkImageLayout& layout = swapchain->SwapchainImageLayouts[image_index];
        if (layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            return;

        VkImageMemoryBarrier2 transition = {};
        transition.sType                  = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        transition.srcStageMask           = layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_2_NONE;
        transition.srcAccessMask          = layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ? VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0;
        transition.dstStageMask           = VK_PIPELINE_STAGE_2_NONE;
        transition.dstAccessMask          = 0;
        transition.oldLayout              = layout;
        transition.newLayout              = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        transition.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        transition.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        transition.image                  = swapchain->SwapchainImages[image_index];
        transition.subresourceRange       = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};

        VkDependencyInfo dependency       = {};
        dependency.sType                  = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers   = &transition;
        PipelineBarrier2(dependency);
        layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    void CommandBuffer::BeginRenderPass(Rendering::Renderers::RenderPasses::GraphicPass* const render_pass, VkFramebuffer framebuffer, bool is_content_secondary_command_buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "command buffer must be Primary Buffer Type")
        ZENGINE_VALIDATE_ASSERT(render_pass != nullptr, "Render pass can't be null")

        const auto&         render_pass_spec = render_pass->Specification;
        const uint32_t      width            = render_pass->GetRenderAreaWidth();
        const uint32_t      height           = render_pass->GetRenderAreaHeight();

        if (Device->PhysicalDeviceSupportDynamicRendering)
        {
            auto* const attachment = render_pass->GetAttachment();
            ZENGINE_VALIDATE_ASSERT(attachment != nullptr, "Dynamic rendering attachment can't be null")

            const auto& attachment_spec = attachment->GetSpecification();
            auto        scratch         = ZGetScratch(&LocalArena);

            Array<VkRenderingAttachmentInfo> color_attachments = {};
            color_attachments.init(scratch.Arena, attachment->GetColorAttachmentCount());

            VkRenderingAttachmentInfo depth_attachment   = {};
            bool                      has_depth_attachment = false;
            bool                      has_stencil_attachment = false;

            const bool uses_swapchain = render_pass_spec.SwapchainAsRenderTarget;
            uint32_t   swapchain_image_index = UINT32_MAX;

            auto is_depth_format = [](VkFormat format) {
                return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_S8_UINT;
            };
            auto has_stencil_component = [](VkFormat format) {
                return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_S8_UINT;
            };

            for (uint32_t attachment_index = 0; attachment_index < attachment_spec.ColorsMap.size(); ++attachment_index)
            {
                const auto& source = attachment_spec.ColorsMap.at(attachment_index);
                VkImageView image_view  = VK_NULL_HANDLE;
                VkFormat    image_format = VK_FORMAT_UNDEFINED;
                VkClearValue clear_value = {};
                bool        is_depth     = false;

                if (uses_swapchain)
                {
                    auto* const swapchain = Device->SwapchainPtr;
                    ZENGINE_VALIDATE_ASSERT(swapchain != nullptr && swapchain->CurrentFrame != nullptr, "Dynamic swapchain rendering has no current frame")
                    swapchain_image_index = swapchain->CurrentFrame->ImageIndex;
                    ZENGINE_VALIDATE_ASSERT(swapchain_image_index < swapchain->SwapchainImageViews.size() && swapchain_image_index < swapchain->SwapchainImages.size() && swapchain_image_index < swapchain->SwapchainImageLayouts.size(), "Dynamic swapchain image index is invalid")
                    image_view   = swapchain->SwapchainImageViews[swapchain_image_index];
                    image_format = Device->SurfaceFormat.format;
                }
                else
                {
                    Textures::TextureHandle texture_handle = {};
                    if (attachment_index < render_pass_spec.Inputs.size())
                        texture_handle = render_pass_spec.Inputs[attachment_index];
                    else
                    {
                        const uint32_t output_index = attachment_index - static_cast<uint32_t>(render_pass_spec.Inputs.size());
                        ZENGINE_VALIDATE_ASSERT(output_index < render_pass_spec.ExternalOutputs.size(), "Dynamic rendering attachment does not have a texture")
                        texture_handle = render_pass_spec.ExternalOutputs[output_index];
                    }

                    auto* const texture = Device->GlobalTextures.Access(texture_handle);
                    ZENGINE_VALIDATE_ASSERT(texture != nullptr, "Dynamic rendering texture is unavailable")
                    auto* const image_buffer = texture ? Device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
                    ZENGINE_VALIDATE_ASSERT(image_buffer != nullptr, "Dynamic rendering image buffer is unavailable")
                    if (image_buffer)
                    {
                        image_view   = image_buffer->GetImageViewHandle();
                        image_format = image_buffer->Specification.ImageFormat;
                        is_depth     = texture->IsDepthTexture;
                        clear_value  = GetTextureClearValue(*texture);
                    }
                }

                ZENGINE_VALIDATE_ASSERT(image_view != VK_NULL_HANDLE, "Dynamic rendering attachment image view is null")
                is_depth = is_depth || is_depth_format(image_format);

                VkRenderingAttachmentInfo rendering_attachment = {};
                rendering_attachment.sType                      = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                rendering_attachment.imageView                  = image_view;
                rendering_attachment.imageLayout                = ImageLayoutMap[VALUE_FROM_SPEC_MAP(source.ReferenceLayout)];
                rendering_attachment.resolveMode                = VK_RESOLVE_MODE_NONE;
                rendering_attachment.resolveImageView           = VK_NULL_HANDLE;
                rendering_attachment.resolveImageLayout         = VK_IMAGE_LAYOUT_UNDEFINED;
                rendering_attachment.loadOp                     = AttachmentLoadOperationMap[VALUE_FROM_SPEC_MAP(source.Load)];
                rendering_attachment.storeOp                    = AttachmentStoreOperationMap[VALUE_FROM_SPEC_MAP(source.Store)];
                rendering_attachment.clearValue                 = clear_value;

                if (is_depth)
                {
                    ZENGINE_VALIDATE_ASSERT(!has_depth_attachment, "Dynamic rendering supports one depth attachment per pass")
                    depth_attachment       = rendering_attachment;
                    has_depth_attachment   = true;
                    has_stencil_attachment = has_stencil_component(image_format);
                }
                else
                {
                    color_attachments.push(rendering_attachment);
                }
            }

            if (uses_swapchain)
                TransitionSwapchainImageToColorAttachment();

            VkRenderingInfo rendering_info               = {};
            rendering_info.sType                          = VK_STRUCTURE_TYPE_RENDERING_INFO;
            rendering_info.flags                          = is_content_secondary_command_buffer ? VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT : 0;
            rendering_info.renderArea                     = {.offset = {0, 0}, .extent = {.width = width, .height = height}};
            rendering_info.layerCount                     = 1;
            rendering_info.viewMask                       = 0;
            rendering_info.colorAttachmentCount           = static_cast<uint32_t>(color_attachments.size());
            rendering_info.pColorAttachments              = color_attachments.data();
            rendering_info.pDepthAttachment               = has_depth_attachment ? &depth_attachment : nullptr;
            rendering_info.pStencilAttachment             = has_stencil_attachment ? &depth_attachment : nullptr;

            BeginDynamicRendering(rendering_info);
            m_dynamic_swapchain_rendering = uses_swapchain;
            ZReleaseScratch(scratch);
            return;
        }

        auto                scratch          = ZGetScratch(&LocalArena);

        Array<VkClearValue> clear_values     = {};
        clear_values.init(scratch.Arena, 5);
        if (render_pass_spec.SwapchainAsRenderTarget)
        {
            clear_values.push({});
        }
        else
        {
            auto& spec = render_pass->Specification;
            for (const auto& handle : spec.Inputs)
            {
                auto texture = Device->GlobalTextures.Access(handle);
                if (texture->IsDepthTexture)
                {
                    clear_values.push(GetTextureClearValue(*texture));
                    continue;
                }
                clear_values.push(GetTextureClearValue(*texture));
            }

            for (const auto& handle : spec.ExternalOutputs)
            {
                auto texture = Device->GlobalTextures.Access(handle);

                if (texture->IsDepthTexture)
                {
                    clear_values.push(GetTextureClearValue(*texture));
                    continue;
                }
                clear_values.push(GetTextureClearValue(*texture));
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

        m_in_render_pass = true;

        ZReleaseScratch(scratch);
    }

    void CommandBuffer::EndRenderPass()
    {
        if (m_in_render_pass)
        {
            ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
            if (m_in_dynamic_rendering)
            {
                const bool     was_swapchain = m_dynamic_swapchain_rendering;
                EndDynamicRendering();

                if (was_swapchain)
                    TransitionSwapchainImageToPresent();
                m_dynamic_swapchain_rendering = false;
                return;
            }
            vkCmdEndRenderPass(m_command_buffer);
            m_active_pipeline = nullptr;
            m_in_render_pass  = false;
        }
    }

    void CommandBuffer::BindDescriptorSets(uint32_t frame_index, const uint32_t* dynamic_offsets, uint32_t dynamic_offset_count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (m_active_pipeline)
        {
            auto                   pipeline_layout    = m_active_pipeline->Layout;
            auto                   shader             = m_active_pipeline->Shader;
            const auto&            set_layout         = shader->SetLayouts;
            const auto&            descriptor_set_map = shader->DescriptorSetMap;

            auto                   scratch            = ZGetScratch(&LocalArena);
            Array<VkDescriptorSet> frame_sets         = {};
            frame_sets.init(scratch.Arena, set_layout.size());

            for (uint32_t i = 0; i < set_layout.size(); ++i)
            {
                const Array<VkDescriptorSet>* set_array = descriptor_set_map.find(i);
                if (!set_array)
                    set_array = Device->ShaderReservedDescriptorSetMap.find(i);

                ZENGINE_VALIDATE_ASSERT(set_array != nullptr && frame_index < set_array->size(), "Descriptor set is unavailable for the active frame")
                if (set_array && frame_index < set_array->size())
                {
                    frame_sets.push((*set_array)[frame_index]);
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
                vkCmdBindDescriptorSets(m_command_buffer, m_active_pipeline->GetBindPoint(), pipeline_layout, 0, frame_sets.size(), frame_sets.data(), count, offsets);
            }
            ZReleaseScratch(scratch);
        }
    }

    void CommandBuffer::BindDescriptorSet(const VkDescriptorSet& descriptor)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(descriptor != nullptr, "DescriptorSet can't be null")
        if (m_active_pipeline)
        {
            VkDescriptorSet desc_set[1] = {descriptor};
            vkCmdBindDescriptorSets(m_command_buffer, m_active_pipeline->GetBindPoint(), m_active_pipeline->Layout, 0, 1, desc_set, 0, nullptr);
        }
    }

    void CommandBuffer::BindPipeline(Rendering::Renderers::Pipelines::IPipeline* const pipeline)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(pipeline != nullptr, "Pipeline can't be null")
        if (!pipeline->EnsureCurrent())
        {
            m_active_pipeline = nullptr;
            return;
        }

        vkCmdBindPipeline(m_command_buffer, pipeline->GetBindPoint(), pipeline->Handle);
        m_active_pipeline = pipeline;
    }

    void CommandBuffer::DrawIndirect(VkBuffer buffer, uint32_t offset, uint32_t draw_count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (m_active_pipeline && buffer != VK_NULL_HANDLE && draw_count > 0)
        {
            vkCmdDrawIndirect(m_command_buffer, buffer, offset, draw_count, sizeof(VkDrawIndirectCommand));
        }
    }

    void CommandBuffer::DrawIndexedIndirect(VkBuffer buffer, uint32_t offset, uint32_t count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (m_active_pipeline && buffer != VK_NULL_HANDLE && count > 0)
        {
            vkCmdDrawIndexedIndirect(m_command_buffer, buffer, offset, count, sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    void CommandBuffer::Dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (m_active_pipeline && group_count_x > 0 && group_count_y > 0 && group_count_z > 0)
            vkCmdDispatch(m_command_buffer, group_count_x, group_count_y, group_count_z);
    }

    void CommandBuffer::DrawIndexed(uint32_t index_count, uint32_t instanceCount, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (m_active_pipeline)
            vkCmdDrawIndexed(m_command_buffer, index_count, instanceCount, first_index, vertex_offset, first_instance);
    }

    void CommandBuffer::Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_index, uint32_t first_instance)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (m_active_pipeline)
            vkCmdDraw(m_command_buffer, vertex_count, instance_count, first_index, first_instance);
    }

    void CommandBuffer::PipelineBarrier2(const VkDependencyInfo& dependency_info)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        vkCmdPipelineBarrier2(m_command_buffer, &dependency_info);
    }

    void CommandBuffer::BeginConditionalRendering(VkBuffer condition_buffer, VkDeviceSize offset, bool invert)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Conditional rendering command buffer is null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "Conditional rendering must begin on a primary command buffer")
        ZENGINE_VALIDATE_ASSERT(!m_in_conditional_rendering, "Conditional rendering is already active")
        ZENGINE_VALIDATE_ASSERT(condition_buffer != VK_NULL_HANDLE, "Conditional rendering requires a condition buffer")
        ZENGINE_VALIDATE_ASSERT(Device->PhysicalDeviceSupportConditionalRendering && Device->__beginConditionalRenderingPtr != nullptr, "Conditional rendering is unavailable")

        const VkConditionalRenderingBeginInfoEXT begin_info = {
            .sType  = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT,
            .buffer = condition_buffer,
            .offset = offset,
            .flags  = invert ? static_cast<VkConditionalRenderingFlagsEXT>(VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT) : 0u,
        };
        Device->__beginConditionalRenderingPtr(m_command_buffer, &begin_info);
        m_in_conditional_rendering = true;
    }

    void CommandBuffer::EndConditionalRendering()
    {
        if (!m_in_conditional_rendering)
            return;

        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Conditional rendering command buffer is null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "Conditional rendering must end on a primary command buffer")
        ZENGINE_VALIDATE_ASSERT(Device->PhysicalDeviceSupportConditionalRendering && Device->__endConditionalRenderingPtr != nullptr, "Conditional rendering is unavailable")

        Device->__endConditionalRenderingPtr(m_command_buffer);
        m_in_conditional_rendering = false;
    }

    void CommandBuffer::WriteTimestamp2(VkPipelineStageFlags2 stage_mask, VkQueryPool query_pool, uint32_t query)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(query_pool != VK_NULL_HANDLE, "Timestamp query pool can't be null")

        vkCmdWriteTimestamp2(m_command_buffer, stage_mask, query_pool, query);
    }

    void CommandBuffer::ResetQueryPool(VkQueryPool query_pool, uint32_t first_query, uint32_t query_count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(query_pool != VK_NULL_HANDLE, "Query pool can't be null")
        ZENGINE_VALIDATE_ASSERT(query_count > 0, "Query reset requires a non-empty range")
        if (query_pool == VK_NULL_HANDLE || query_count == 0)
            return;

        vkCmdResetQueryPool(m_command_buffer, query_pool, first_query, query_count);
    }

    void CommandBuffer::BeginQuery(VkQueryPool query_pool, uint32_t query, VkQueryControlFlags flags)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(query_pool != VK_NULL_HANDLE, "Query pool can't be null")
        if (query_pool == VK_NULL_HANDLE)
            return;

        vkCmdBeginQuery(m_command_buffer, query_pool, query, flags);
    }

    void CommandBuffer::EndQuery(VkQueryPool query_pool, uint32_t query)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(query_pool != VK_NULL_HANDLE, "Query pool can't be null")
        if (query_pool == VK_NULL_HANDLE)
            return;

        vkCmdEndQuery(m_command_buffer, query_pool, query);
    }

    void CommandBuffer::CopyQueryPoolResults(VkQueryPool query_pool, uint32_t first_query, uint32_t query_count, VkBuffer destination, VkDeviceSize destination_offset, VkDeviceSize stride, VkQueryResultFlags flags)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(query_pool != VK_NULL_HANDLE, "Query pool can't be null")
        ZENGINE_VALIDATE_ASSERT(destination != VK_NULL_HANDLE, "Query result destination buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(query_count > 0 && stride > 0, "Query result copy requires a non-empty range and stride")
        if (query_pool == VK_NULL_HANDLE || destination == VK_NULL_HANDLE || query_count == 0 || stride == 0)
            return;

        vkCmdCopyQueryPoolResults(m_command_buffer, query_pool, first_query, query_count, destination, destination_offset, stride, flags);
    }

    void CommandBuffer::BeginDebugLabel(cstring name)
    {
        Device->BeginDebugLabel(m_command_buffer, name);
    }

    void CommandBuffer::EndDebugLabel()
    {
        Device->EndDebugLabel(m_command_buffer);
    }

    void CommandBuffer::TransitionImageLayout(const Rendering::Primitives::ImageMemoryBarrier& image_barrier)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        const auto& barrier_handle = image_barrier.GetHandle();
        const auto& barrier_spec   = image_barrier.GetSpecification();
        vkCmdPipelineBarrier(m_command_buffer, barrier_spec.SourceStageMask, barrier_spec.DestinationStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier_handle);
    }

    void CommandBuffer::CopyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size, VkDeviceSize source_offset, VkDeviceSize destination_offset)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != VK_NULL_HANDLE, "Copy buffer command buffer is null")
        ZENGINE_VALIDATE_ASSERT(source != VK_NULL_HANDLE && destination != VK_NULL_HANDLE, "CopyBuffer requires valid source and destination buffers")
        if (size == 0 || source == VK_NULL_HANDLE || destination == VK_NULL_HANDLE)
            return;

        const VkBufferCopy region = {
            .srcOffset = source_offset,
            .dstOffset = destination_offset,
            .size      = size,
        };
        vkCmdCopyBuffer(m_command_buffer, source, destination, 1, &region);
    }

    void CommandBuffer::CopyBufferToImage(const Hardwares::BufferView& source, Hardwares::BufferImage& destination, uint32_t width, uint32_t height, uint32_t layer_count, VkImageLayout new_layout, uint32_t source_offset)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        VkBufferImageCopy buffer_image_copy               = {};
        buffer_image_copy.bufferOffset                    = source_offset;
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

        if (m_active_pipeline)
        {
            vkCmdPushConstants(m_command_buffer, m_active_pipeline->Layout, stage_flags, offset, size, data);
        }
    }

    void CommandBuffer::ExecuteSecondaryCommandBuffer(CommandBuffer* const buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(BufferType == CommandBufferType::Primary, "command buffer must be Primary Buffer Type")
        ZENGINE_VALIDATE_ASSERT(buffer != nullptr && buffer->BufferType == CommandBufferType::Secondary, "secondary command buffer is invalid")
        ZENGINE_VALIDATE_ASSERT(buffer->IsExecutable(), "secondary command buffer must be executable")

        const VkCommandBuffer handle = buffer->GetHandle();
        vkCmdExecuteCommands(m_command_buffer, 1, &handle);
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

    void ImageBuffer::Construct(Hardwares::VulkanDevice* device)
    {
        Device = device;
        Layout = Rendering::Specifications::ImageLayout::UNDEFINED;
        if (m_cached_image_views.data())
            m_cached_image_views.clear();
        else
            m_cached_image_views.init(Device->Arena, 4);
        ZENGINE_VALIDATE_ASSERT(Specification.Width > 0, "Image width must be greater then zero")
        ZENGINE_VALIDATE_ASSERT(Specification.Height > 0, "Image height must be greater then zero")

        Specifications::ImageViewType   image_view_type   = Specifications::ImageViewType::TYPE_2D;
        Specifications::ImageCreateFlag image_create_flag = Specifications::ImageCreateFlag::NONE;

        if (Specification.BufferUsageType == Specifications::ImageBufferUsageType::CUBEMAP)
        {
            image_view_type   = Specifications::ImageViewType::TYPE_CUBE;
            image_create_flag = Specifications::ImageCreateFlag::CUBE_COMPATIBLE_BIT;
        }

        VkImageCreateFlags image_create_flags = Specifications::ImageCreateFlagMap[VALUE_FROM_SPEC_MAP(image_create_flag)];
        if (Specification.IsAliasable)
            image_create_flags |= VK_IMAGE_CREATE_ALIAS_BIT;
        m_buffer_image = Device->CreateImage(Specification.Width, Specification.Height, VK_IMAGE_TYPE_2D, Specifications::ImageViewTypeMap[VALUE_FROM_SPEC_MAP(image_view_type)], Specification.ImageFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED, Specification.ImageUsage, VK_SHARING_MODE_EXCLUSIVE, VK_SAMPLE_COUNT_1_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Specification.ImageAspectFlag, Specification.LayerCount, Specification.MipLevelCount, image_create_flags, DebugName);
    }

    void ImageBuffer::ConstructAliasing(Hardwares::VulkanDevice* device, const BufferImage& backing)
    {
        Device = device;
        Layout = Rendering::Specifications::ImageLayout::UNDEFINED;
        if (m_cached_image_views.data())
            m_cached_image_views.clear();
        else
            m_cached_image_views.init(Device->Arena, 4);
        ZENGINE_VALIDATE_ASSERT(Specification.Width > 0, "Image width must be greater then zero")
        ZENGINE_VALIDATE_ASSERT(Specification.Height > 0, "Image height must be greater then zero")

        Specifications::ImageViewType   image_view_type   = Specifications::ImageViewType::TYPE_2D;
        Specifications::ImageCreateFlag image_create_flag = Specifications::ImageCreateFlag::NONE;
        if (Specification.BufferUsageType == Specifications::ImageBufferUsageType::CUBEMAP)
        {
            image_view_type   = Specifications::ImageViewType::TYPE_CUBE;
            image_create_flag = Specifications::ImageCreateFlag::CUBE_COMPATIBLE_BIT;
        }

        VkImageCreateFlags image_create_flags = Specifications::ImageCreateFlagMap[VALUE_FROM_SPEC_MAP(image_create_flag)];
        if (Specification.IsAliasable)
            image_create_flags |= VK_IMAGE_CREATE_ALIAS_BIT;
        m_buffer_image = Device->CreateAliasingImage(backing, Specification.Width, Specification.Height, VK_IMAGE_TYPE_2D, Specifications::ImageViewTypeMap[VALUE_FROM_SPEC_MAP(image_view_type)], Specification.ImageFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED, Specification.ImageUsage, VK_SHARING_MODE_EXCLUSIVE, VK_SAMPLE_COUNT_1_BIT, Specification.ImageAspectFlag, Specification.LayerCount, Specification.MipLevelCount, image_create_flags, DebugName);
    }

    ImageBuffer::~ImageBuffer()
    {
        Dispose();
    }

    BufferImage& ImageBuffer::GetBuffer()
    {
        return m_buffer_image;
    }

    const BufferImage& ImageBuffer::GetBuffer() const
    {
        return m_buffer_image;
    }

    VkImage ImageBuffer::GetHandle() const
    {
        return m_buffer_image.Handle;
    }

    VkSampler ImageBuffer::GetSampler() const
    {
        return m_buffer_image.Sampler;
    }

    void ImageBuffer::Dispose()
    {
        for (const auto& view : m_cached_image_views)
        {
            if (view.Handle == VK_NULL_HANDLE)
                continue;
            DeferredFreeEntry e = {};
            e.EntryKind         = DeferredFreeEntry::Kind::VkHandle;
            e.Data.Vk           = {view.Handle, Rendering::DeviceResourceType::IMAGEVIEW, nullptr};
            Device->DeferFree(e);
        }
        m_cached_image_views.clear();

        if (m_buffer_image)
        {
            DeferredFreeEntry e  = {};
            e.EntryKind          = DeferredFreeEntry::Kind::Image;
            e.Data.Image         = m_buffer_image;
            Device->DeferFree(e);
            m_buffer_image = {};
        }
    }

    VkDescriptorImageInfo& ImageBuffer::GetDescriptorImageInfo()
    {
        m_image_info.sampler     = m_buffer_image.Sampler;
        m_image_info.imageView   = m_buffer_image.ViewHandle;
        m_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return m_image_info;
    }

    VkImageView ImageBuffer::GetImageViewHandle() const
    {
        return m_buffer_image.ViewHandle;
    }

    VkImageView ImageBuffer::GetImageViewHandle(VkImageViewType view_type, const VkImageSubresourceRange& range)
    {
        if (m_buffer_image.Handle == VK_NULL_HANDLE || range.aspectMask == 0 || range.levelCount == 0 || range.layerCount == 0)
            return VK_NULL_HANDLE;

        const bool is_full_default_view = view_type == Specifications::ImageViewTypeMap[VALUE_FROM_SPEC_MAP(Specification.ImageViewTypeValue)] && range.aspectMask == Specification.ImageAspectFlag && range.baseMipLevel == 0 && range.levelCount == Specification.MipLevelCount && range.baseArrayLayer == 0 && range.layerCount == Specification.LayerCount;
        if (is_full_default_view)
            return m_buffer_image.ViewHandle;

        for (const auto& view : m_cached_image_views)
        {
            if (view.ViewType == view_type && view.Range.aspectMask == range.aspectMask && view.Range.baseMipLevel == range.baseMipLevel && view.Range.levelCount == range.levelCount && view.Range.baseArrayLayer == range.baseArrayLayer && view.Range.layerCount == range.layerCount)
                return view.Handle;
        }

        const VkImageView image_view = Device->CreateImageView(m_buffer_image.Handle, Specification.ImageFormat, view_type, range);
        if (image_view == VK_NULL_HANDLE)
            return VK_NULL_HANDLE;
        m_cached_image_views.push({.Range = range, .ViewType = view_type, .Handle = image_view});
        return image_view;
    }











    // Shared by CreateTexture and ReconstructTexture: derives Texture metadata and the
    // ImageBuffer's Specification from a TextureSpecification. Does not call Construct().
    static void PopulateTextureResource(const Rendering::Specifications::TextureSpecification& spec, Rendering::Textures::Texture* resource, ImageBuffer* buffer_res, VulkanDevice* device)
    {
        resource->Specification         = spec;
        resource->Width                 = spec.Width;
        resource->Height                = spec.Height;
        resource->BytePerPixel          = spec.BytePerPixel;
        resource->BufferSize            = 0;
        for (uint32_t mip = 0; mip < spec.MipLevelCount; ++mip)
        {
            const uint32_t mip_width  = std::max(1u, spec.Width >> mip);
            const uint32_t mip_height = std::max(1u, spec.Height >> mip);
            resource->BufferSize += static_cast<VkDeviceSize>(mip_width) * mip_height * spec.BytePerPixel * spec.LayerCount;
        }
        resource->IsDepthTexture        = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);

        uint32_t storage_bit            = spec.IsUsageStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
        uint32_t transfert_dst_bit      = spec.IsUsageTransfert ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
        uint32_t transfert_src_bit      = spec.IsUsageTransferSource ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0;
        uint32_t sampled_bit            = spec.IsUsageSampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
        uint32_t image_aspect           = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t image_usage_attachment = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkFormat image_format            = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? device->FindDepthFormat() : Specifications::ImageFormatMap[VALUE_FROM_SPEC_MAP(spec.Format)];

        buffer_res->Specification            = {.Width = spec.Width, .Height = spec.Height, .ImageViewTypeValue = spec.IsCubemap ? Specifications::ImageViewType::TYPE_CUBE : Specifications::ImageViewType::TYPE_2D, .BufferUsageType = spec.IsCubemap ? Specifications::ImageBufferUsageType::CUBEMAP : Specifications::ImageBufferUsageType::SINGLE_2D_IMAGE, .ImageFormat = image_format, .ImageAspectFlag = VkImageAspectFlagBits(image_aspect), .MipLevelCount = spec.MipLevelCount, .LayerCount = spec.LayerCount};
        buffer_res->Specification.ImageUsage = VkImageUsageFlagBits(image_usage_attachment | transfert_dst_bit | transfert_src_bit | sampled_bit | storage_bit);
        buffer_res->Specification.IsAliasable = spec.IsAliasable;
    }

    Rendering::Textures::TextureHandle VulkanDevice::CreateTexture(const Rendering::Specifications::TextureSpecification& spec, cstring debug_name)
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

        auto buff_handle       = ImageBufferManager.Create();
        auto buffer_res        = ImageBufferManager.Access(buff_handle);

        buffer_res->DebugName = debug_name;
        PopulateTextureResource(spec, resource, buffer_res, this);
        buffer_res->Construct(this);

        resource->BufferHandle = buff_handle;

        return handle;
    }

    Rendering::Textures::TextureHandle VulkanDevice::CreateAliasingTexture(const Rendering::Specifications::TextureSpecification& spec, const Rendering::Textures::TextureHandle& source, cstring debug_name)
    {
        std::unique_lock lock(Mutex);

        auto* const source_texture = GlobalTextures.Access(source);
        if (!source_texture)
            return {};
        auto* const source_image = ImageBufferManager.Access(source_texture->BufferHandle);
        if (!source_image || !source_image->GetBuffer().OwnsAllocation)
            return {};

        const auto handle = GlobalTextures.Create();
        if (!handle)
            return {};
        auto* const texture = GlobalTextures.Access(handle);
        if (!texture)
            return {};

        const auto buffer_handle = ImageBufferManager.Create();
        auto* const buffer       = ImageBufferManager.Access(buffer_handle);
        if (!buffer)
        {
            auto stale_texture = handle;
            GlobalTextures.Remove(stale_texture);
            return {};
        }

        buffer->DebugName = debug_name;
        PopulateTextureResource(spec, texture, buffer, this);
        buffer->ConstructAliasing(this, source_image->GetBuffer());
        if (!buffer->GetBuffer())
        {
            auto stale_buffer  = buffer_handle;
            auto stale_texture = handle;
            ImageBufferManager.Remove(stale_buffer);
            GlobalTextures.Remove(stale_texture);
            return {};
        }

        texture->BufferHandle = buffer_handle;
        return handle;
    }

    bool VulkanDevice::ReconstructTexture(const Rendering::Textures::TextureHandle& handle, const Rendering::Specifications::TextureSpecification& spec)
    {
        std::unique_lock l(Mutex);

        auto             resource = GlobalTextures.Access(handle);
        if (!resource)
        {
            return false;
        }

        auto buffer_res = ImageBufferManager.Access(resource->BufferHandle);
        if (!buffer_res)
        {
            return false;
        }

        // Dispose the old default and subresource views before constructing their
        // replacement. All handles are timeline-deferred as one safe retirement set.
        buffer_res->Dispose();
        PopulateTextureResource(spec, resource, buffer_res, this);
        buffer_res->Construct(this);

        return true;
    }

    bool VulkanDevice::ReconstructAliasingTexture(const Rendering::Textures::TextureHandle& handle, const Rendering::Specifications::TextureSpecification& spec, const Rendering::Textures::TextureHandle& source)
    {
        std::unique_lock lock(Mutex);

        auto* const texture        = GlobalTextures.Access(handle);
        auto* const source_texture = GlobalTextures.Access(source);
        if (!texture || !source_texture)
            return false;
        auto* const image        = ImageBufferManager.Access(texture->BufferHandle);
        auto* const source_image = ImageBufferManager.Access(source_texture->BufferHandle);
        if (!image || !source_image || !source_image->GetBuffer().OwnsAllocation)
            return false;

        image->Dispose();
        PopulateTextureResource(spec, texture, image, this);
        image->ConstructAliasing(this, source_image->GetBuffer());
        return static_cast<bool>(image->GetBuffer());
    }

    void VulkanDevice::FlushBindlessTextureUpdates()
    {
        static bool watermark_warned = false;
        if (!watermark_warned)
        {
            const size_t   live     = GlobalTextures.Size();
            const uint32_t capacity = MaxGlobalTexture;
            if (live > static_cast<size_t>(capacity) * 3 / 4)
            {
                ZENGINE_CORE_WARN("[Bindless] Texture pool at {}/{} slots ({:.0f}%) — consider releasing unused textures", live, capacity, live * 100.0 / capacity)
                watermark_warned = true;
            }
        }

        auto scratch = ZGetScratch(Arena);
        {
            const size_t              request_count = BindlessTextureSlotRequests.size();
            Array<VkWriteDescriptorSet> write_descriptor_sets = {};
            Array<VkDescriptorImageInfo> image_infos = {};
            write_descriptor_sets.init(scratch.Arena, request_count > 0 ? request_count * 64 : 1);
            image_infos.init(scratch.Arena, 64);

            Rendering::Textures::TextureHandle texture_handle = {};
            while (TextureHandleToUpdates.pop(texture_handle))
            {
                const auto* texture = GlobalTextures.Access(texture_handle);
                if (!texture)
                {
                    RequestDescriptorUpdate(texture_handle);
                    break;
                }
                auto* image_buffer = ImageBufferManager.Access(texture->BufferHandle);
                if (!image_buffer)
                    continue;
                image_infos.push(image_buffer->GetDescriptorImageInfo());
                const VkDescriptorImageInfo* image_info = &image_infos[image_infos.size() - 1];
                for (const auto& request : BindlessTextureSlotRequests)
                {
                    write_descriptor_sets.push({
                        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet           = request.DstSet,
                        .dstBinding       = request.Binding,
                        .dstArrayElement  = static_cast<uint32_t>(texture_handle.Index),
                        .descriptorCount  = 1,
                        .descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        .pImageInfo       = image_info,
                    });
                }
            }
            if (!write_descriptor_sets.empty())
                vkUpdateDescriptorSets(LogicalDevice, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
        }
        ZReleaseScratch(scratch);

        // Preserve the existing one-frame fallback: the real descriptor write is
        // requeued and therefore becomes visible at the next graph frame begin.
        Rendering::Textures::TextureHandle deferred_handle = {};
        while (DeferredTextureDescriptorUpdates.pop(deferred_handle))
        {
            if (FallbackDescriptorImageInfo.imageView != VK_NULL_HANDLE)
            {
                for (const auto& request : BindlessTextureSlotRequests)
                {
                    VkWriteDescriptorSet write = {
                        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet          = request.DstSet,
                        .dstBinding      = request.Binding,
                        .dstArrayElement = static_cast<uint32_t>(deferred_handle.Index),
                        .descriptorCount = 1,
                        .descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        .pImageInfo      = &FallbackDescriptorImageInfo,
                    };
                    vkUpdateDescriptorSets(LogicalDevice, 1, &write, 0, nullptr);
                }
            }
            RequestDescriptorUpdate(deferred_handle);
        }
    }

    void VulkanDevice::RequestDescriptorUpdate(const Rendering::Textures::TextureHandle& handle)
    {
        if (!TextureHandleToUpdates.push(handle))
            ZENGINE_CORE_ERROR("[Bindless] Texture descriptor-update queue overflow — texture handle (index {}) was not published", handle.Index)
    }

    void VulkanDevice::DestroyTexture(const Rendering::Textures::TextureHandle& handle)
    {
        TextureDisposeEntry entry = {};
        entry.Handle              = handle;
        entry.TimelineValue       = SwapchainPtr->RenderTimelineNextValue;
        if (!TextureHandleToDispose.push(entry))
        {
            ZENGINE_CORE_ERROR("[!] TextureHandleToDispose overflow — texture handle (index {}) leaked", handle.Index)
        }
    }

    BufferView VulkanDevice::WriteTextureData(CommandBufferPtr command_buf, const Rendering::Textures::TextureHandle& handle, const void* data, uint32_t* out_ring_offset, bool use_staging_ring)
    {
        if (out_ring_offset)
            *out_ring_offset = std::numeric_limits<uint32_t>::max();

        if (!handle.Valid() || !(data) || !(command_buf))
        {
            return {};
        }

        auto     resource    = GlobalTextures.Access(handle);
        auto     image_buf   = ImageBufferManager.Access(resource->BufferHandle);

        uint32_t ring_offset = 0;
        void*    ring_ptr    = use_staging_ring ? GpuMem.Ring.Allocate(static_cast<uint32_t>(resource->BufferSize), 4, &ring_offset) : nullptr;

        if (ring_ptr)
        {
            Helpers::secure_memcpy(ring_ptr, resource->BufferSize, data, resource->BufferSize);
            BufferView ring_view = {};
            ring_view.Handle     = GpuMem.Ring.Buffer;
            ring_view.Allocation = GpuMem.Ring.Allocation;
            command_buf->CopyBufferToImage(ring_view, image_buf->GetBuffer(), resource->Width, resource->Height, resource->Specification.LayerCount, Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(image_buf->Layout)], ring_offset);
            // Ring owns lifetime, caller must not free — but must call GpuMem.Ring.Submit
            // once it knows the timeline value this copy will signal (see out_ring_offset).
            if (out_ring_offset)
                *out_ring_offset = ring_offset;
            return {};
        }

        BufferView staging_view = CreateBuffer(resource->BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Core::Memory::GpuMemoryDomain::HostStaging, "WriteTextureData_staging");
        MapAndCopyToMemory(staging_view, resource->BufferSize, data);
        command_buf->CopyBufferToImage(staging_view, image_buf->GetBuffer(), resource->Width, resource->Height, resource->Specification.LayerCount, Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(image_buf->Layout)]);
        return staging_view;
    }

    Rendering::Renderers::RenderPasses::RenderPass* VulkanDevice::CreateRenderPass(Rendering::Specifications::RenderPassSpecification spec)
    {
        if (spec.Type == Rendering::Specifications::RenderPassType::COMPUTE)
        {
            auto pass = ZPushStructCtorArgs(Arena, Rendering::Renderers::RenderPasses::ComputePass);
            pass->Initialize(this, std::move(spec));
            return pass;
        }
        auto pass = ZPushStructCtorArgs(Arena, Rendering::Renderers::RenderPasses::GraphicPass);
        pass->Initialize(this, std::move(spec));
        return pass;
    }

    void VulkanDevice::TickMemory()
    {
        uint64_t completed = 0;
        vkGetSemaphoreCounterValue(LogicalDevice, SwapchainPtr->RenderTimeline->GetHandle(), &completed);
        PendingFree.Drain(&GpuMem, LogicalDevice, completed);
        GpuMem.Ring.Drain(completed);
        GpuMem.SampleBudgets();
        if (PipelineStateCache)
            PipelineStateCache->EvictUnusedPipelines(SwapchainPtr->RenderTimelineNextValue);

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
        ZENGINE_VALIDATE_ASSERT(AsyncGPUOperations.push(operation), "Async GPU operation queue overflow")
    }

    void VulkanDevice::EnqueueDeferredAsyncGPUOperation(const AsyncGPUOperationHandle& operation)
    {
        DeferredAsyncGPUOperations.push(operation);
    }

    void VulkanDevice::AddBindlessTextureSlotRequest(const WriteDescriptorSetRequestKey& request)
    {
        for (const WriteDescriptorSetRequestKey& existing : BindlessTextureSlotRequests)
        {
            if (existing == request)
                return;
        }
        BindlessTextureSlotRequests.push(request);
    }

    void VulkanDevice::RequestDeferredDescriptorUpdate(const Rendering::Textures::TextureHandle& handle)
    {
        // Push to the deferred queue only — the render thread drains this and writes
        // the fallback then the real image, both from a single thread so vkUpdateDescriptorSets
        // is never called concurrently with Present()'s own descriptor batch.
        DeferredTextureDescriptorUpdates.push(handle);
    }

} // namespace ZEngine::Hardwares
