#include <ZEngineDef.h>

/*
 * We define those Macros before inclusion of VulkanDevice.h so we can enable impl from VMA header
 */
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1003000 // Vulkan 1.3

#include <Hardwares/VulkanDevice.h>
#include <Helpers/MemoryOperations.h>
#include <Helpers/ThreadPool.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Pools/CommandPool.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Windows/CoreWindow.h>

using namespace std::chrono_literals;
using namespace ZEngine::Rendering::Primitives;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Hardwares
{
    void VulkanDevice::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::CoreWindow* const window)
    {
        Arena          = arena;
        CurrentWindow  = window;
        AsyncResLoader = ZPushStructCtor(Arena, AsyncResourceLoader);

        DefaultDepthFormats.init(Arena, 3);
        DefaultDepthFormats.push(VK_FORMAT_D32_SFLOAT);
        DefaultDepthFormats.push(VK_FORMAT_D32_SFLOAT_S8_UINT);
        DefaultDepthFormats.push(VK_FORMAT_D24_UNORM_S8_UINT);

        GlobalTextures.Initialize(arena, 600);
        Image2DBufferManager.Initialize(arena, 600);
        ShaderManager.Initialize(arena, 300);
        VertexBufferSetManager.Initialize(arena, 300);
        StorageBufferSetManager.Initialize(arena, 300);
        IndirectBufferSetManager.Initialize(arena, 300);
        IndexBufferSetManager.Initialize(arena, 300);
        UniformBufferSetManager.Initialize(arena, 300);
        DirtyResources.Initialize(arena, 300);
        DirtyBuffers.Initialize(arena, 500);
        DirtyBufferImages.Initialize(arena, 300);
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
        validation_layer_name_collection.push("VK_LAYER_LUNARG_api_dump");
        validation_layer_name_collection.push("VK_LAYER_KHRONOS_validation");
        validation_layer_name_collection.push("VK_LAYER_LUNARG_monitor");
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
#endif
        instance_create_info.enabledLayerCount       = enabled_layer_name_collection.size();
        instance_create_info.ppEnabledLayerNames     = enabled_layer_name_collection.data();
        instance_create_info.enabledExtensionCount   = enabled_extension_layer_name_collection.size();
        instance_create_info.ppEnabledExtensionNames = enabled_extension_layer_name_collection.data();

        VkResult result                              = vkCreateInstance(&instance_create_info, nullptr, &Instance);

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

        for (VkPhysicalDevice physical_device : physical_device_collection)
        {
            VkPhysicalDeviceDescriptorIndexingProperties indexing_properties = {};
            indexing_properties.sType                                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

            VkPhysicalDeviceProperties  physical_device_properties;
            VkPhysicalDeviceProperties2 physical_device_properties2 = {};
            physical_device_properties2.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            physical_device_properties2.pNext                       = &indexing_properties;

            vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
            vkGetPhysicalDeviceProperties2(physical_device, &physical_device_properties2);

            VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {};
            descriptor_indexing_features.sType                                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
            VkPhysicalDeviceFeatures2 physical_device_feature                       = {};
            physical_device_feature.sType                                           = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            physical_device_feature.pNext                                           = &descriptor_indexing_features;
            vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_feature);

            if (/*(physical_device_feature.geometryShader == VK_TRUE) && (physical_device_feature.samplerAnisotropy == VK_TRUE) && */ ((physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) || (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)))
            {
                PhysicalDevice                                 = physical_device;
                PhysicalDeviceProperties                       = physical_device_properties;
                PhysicalDeviceDescriptorIndexingProperties     = indexing_properties;
                PhysicalDeviceFeature                          = physical_device_feature;
                PhysicalDeviceSupportDescriptorUpdateAfterBind = (descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE && descriptor_indexing_features.descriptorBindingPartiallyBound == VK_TRUE && descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending == VK_TRUE);
                vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &PhysicalDeviceMemoryProperties);
                break;
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
        VkDeviceCreateInfo device_create_info                                                   = {};
        device_create_info.sType                                                                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount                                                 = queue_create_info_collection.size();
        device_create_info.pQueueCreateInfos                                                    = queue_create_info_collection.data();
        device_create_info.enabledExtensionCount                                                = static_cast<uint32_t>(requested_device_extension_layer_name_collection.size());
        device_create_info.ppEnabledExtensionNames                                              = (requested_device_extension_layer_name_collection.size() > 0) ? requested_device_extension_layer_name_collection.data() : nullptr;
        device_create_info.pEnabledFeatures                                                     = nullptr;

        VkPhysicalDeviceDescriptorIndexingFeatures physical_device_descriptor_indexing_features = {};
        physical_device_descriptor_indexing_features.sType                                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        VkPhysicalDeviceFeatures2 device_features_2                                             = {};
        device_features_2.sType                                                                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        device_features_2.features.drawIndirectFirstInstance                                    = PhysicalDeviceFeature.features.drawIndirectFirstInstance;
        device_features_2.features.multiDrawIndirect                                            = PhysicalDeviceFeature.features.multiDrawIndirect;
        device_features_2.features.samplerAnisotropy                                            = PhysicalDeviceFeature.features.samplerAnisotropy;
        device_features_2.features.shaderInt64                                                  = PhysicalDeviceFeature.features.shaderInt64;
        if (PhysicalDeviceSupportDescriptorUpdateAfterBind)
        {
            physical_device_descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing    = VK_TRUE;
            physical_device_descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            physical_device_descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending    = VK_TRUE;
            physical_device_descriptor_indexing_features.descriptorBindingPartiallyBound              = VK_TRUE;
            physical_device_descriptor_indexing_features.runtimeDescriptorArray                       = VK_TRUE;

            device_features_2.pNext                                                                   = &physical_device_descriptor_indexing_features;
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

        ZReleaseScratch(scratch);

        /*
         * Creating VMA Allocators
         */
        VmaAllocatorCreateInfo vma_allocator_create_info = {.physicalDevice = PhysicalDevice, .device = LogicalDevice, .instance = Instance, .vulkanApiVersion = VK_API_VERSION_1_3};
        ZENGINE_VALIDATE_ASSERT(vmaCreateAllocator(&vma_allocator_create_info, &VmaAllocatorValue) == VK_SUCCESS, "Failed to create VMA Allocator")

        m_buffer_manager.Initialize(this);
        EnqueuedCommandbuffers.init(Arena, m_buffer_manager.TotalCommandBufferCount, m_buffer_manager.TotalCommandBufferCount);

        /*
         * Creating Swapchain
         */
        Specifications::AttachmentSpecification attachment_specification = {.BindPoint = Specifications::PipelineBindPoint::GRAPHIC};
        attachment_specification.ColorsMap.init(Arena, 2);
        attachment_specification.ColorsMap[0]                 = {};
        attachment_specification.ColorsMap[0].Format          = ImageFormat::FORMAT_FROM_DEVICE;
        attachment_specification.ColorsMap[0].Load            = LoadOperation::CLEAR;
        attachment_specification.ColorsMap[0].Store           = StoreOperation::STORE;
        attachment_specification.ColorsMap[0].Initial         = ImageLayout::UNDEFINED;
        attachment_specification.ColorsMap[0].Final           = ImageLayout::PRESENT_SRC;
        attachment_specification.ColorsMap[0].ReferenceLayout = ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
        SwapchainAttachment                                   = ZPushStructCtorArgs(Arena, Rendering::Renderers::RenderPasses::Attachment, this, attachment_specification);
        PreviousFrameIndex                                    = 0;
        CurrentFrameIndex                                     = 0;

        SwapchainRenderCompleteSemaphores.init(Arena, SwapchainImageCount, SwapchainImageCount);
        SwapchainAcquiredSemaphores.init(Arena, SwapchainImageCount, SwapchainImageCount);
        SwapchainSignalFences.init(Arena, SwapchainImageCount, SwapchainImageCount);

        for (int i = 0; i < SwapchainImageCount; ++i)
        {
            SwapchainAcquiredSemaphores[i]       = ZPushStructCtorArgs(Arena, Primitives::Semaphore, this);
            SwapchainRenderCompleteSemaphores[i] = ZPushStructCtorArgs(Arena, Primitives::Semaphore, this);
            SwapchainSignalFences[i]             = ZPushStructCtorArgs(Arena, Primitives::Fence, this, true);
        }
        CreateSwapchain();

        AsyncResLoader->Initialize(this);

        ThreadPoolHelper::Submit([this] { DirtyCollector(); });
    }

    void VulkanDevice::Deinitialize()
    {
        QueueWaitAll();
        {
            std::unique_lock l(DirtyMutex);
            RunningDirtyCollector = false;
        }
        DirtyCollectorCond.notify_one();

        AsyncResLoader->Shutdown();

        GlobalTextures.Dispose();
        Image2DBufferManager.Dispose();
        VertexBufferSetManager.Dispose();
        StorageBufferSetManager.Dispose();
        IndirectBufferSetManager.Dispose();
        IndexBufferSetManager.Dispose();
        UniformBufferSetManager.Dispose();
        ShaderManager.Dispose();

        EnqueuedCommandbuffers.clear();
        SwapchainSignalFences.clear();
        SwapchainAcquiredSemaphores.clear();
        SwapchainRenderCompleteSemaphores.clear();

        DisposeSwapchain();
        SwapchainAttachment->Dispose();

        SwapchainFramebuffers.clear();
        SwapchainImageViews.clear();

        m_buffer_manager.Deinitialize();

        __cleanupBufferDirtyResource();

        __cleanupBufferImageDirtyResource();

        __cleanupDirtyResource();

        ZENGINE_DESTROY_VULKAN_HANDLE(Instance, vkDestroySurfaceKHR, Surface, nullptr)
    }

    void VulkanDevice::Update() {}

    void VulkanDevice::Dispose()
    {
        vmaDestroyAllocator(VmaAllocatorValue);

        if (__destroyDebugMessengerPtr)
        {
            ZENGINE_DESTROY_VULKAN_HANDLE(Instance, __destroyDebugMessengerPtr, m_debug_messenger, nullptr)
            __destroyDebugMessengerPtr = nullptr;
            __createDebugMessengerPtr  = nullptr;
        }
        vkDestroyDevice(LogicalDevice, nullptr);
        vkDestroyInstance(Instance, nullptr);

        LogicalDevice = VK_NULL_HANDLE;
        Instance      = VK_NULL_HANDLE;
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
                     .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .pNext                = nullptr,
                     .waitSemaphoreCount   = 0,
                     .pWaitSemaphores      = 0,
                     .pWaitDstStageMask    = flags,
                     .commandBufferCount   = 1,
                     .pCommandBuffers      = buffers,
                     .signalSemaphoreCount = 0,
                     .pSignalSemaphores    = 0,
        };

        ZENGINE_VALIDATE_ASSERT(vkQueueSubmit(GetQueue(command_buffer->QueueType).Handle, 1, &submit_info, fence->GetHandle()) == VK_SUCCESS, "Failed to submit queue")
        command_buffer->SetState(CommanBufferState::Pending);

        fence->SetState(FenceState::Submitted);
        signal_semaphore->SetState(SemaphoreState::Submitted);

        if (!fence->Wait())
        {
            ZENGINE_CORE_WARN("Failed to wait for Command buffer's Fence, due to timeout")
            return false;
        }

        fence->Reset();
        signal_semaphore->SetState(Rendering::Primitives::SemaphoreState::Idle);
        command_buffer->SetState(CommanBufferState::Invalid);

        return true;
    }

    void VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType resource_type, void* const handle)
    {
        if (handle)
        {
            DirtyResources.Add({.FrameIndex = CurrentFrameIndex, .Handle = handle, .Type = resource_type});
        }
    }

    void VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource)
    {
        if (resource.Handle)
        {
            DirtyResources.Add(resource);
        }
    }

    void VulkanDevice::EnqueueBufferForDeletion(BufferView& buffer)
    {
        DirtyBuffers.Add(buffer);
    }

    void VulkanDevice::EnqueueBufferImageForDeletion(BufferImage& buffer)
    {
        DirtyBufferImages.Add(buffer);
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
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            ZENGINE_CORE_ERROR("{}", pCallbackData->pMessage)
        }

        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            ZENGINE_CORE_WARN("{}", pCallbackData->pMessage)
        }

        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        {
            ZENGINE_CORE_WARN("{}", pCallbackData->pMessage)
        }

        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) == VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        {
            ZENGINE_CORE_WARN("{}", pCallbackData->pMessage)
        }

        return VK_FALSE;
    }

    void VulkanDevice::__cleanupDirtyResource()
    {
        size_t dirty_resource_count = DirtyResources.Head();
        for (size_t i = 0; i < dirty_resource_count; ++i)
        {
            auto handle = DirtyResources.ToHandle(i);

            if (!handle)
            {
                continue;
            }

            DirtyResource& res_handle = DirtyResources[handle];
            switch (res_handle.Type)
            {
                case Rendering::DeviceResourceType::SAMPLER:
                    vkDestroySampler(LogicalDevice, reinterpret_cast<VkSampler>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::FRAMEBUFFER:
                    vkDestroyFramebuffer(LogicalDevice, reinterpret_cast<VkFramebuffer>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::IMAGEVIEW:
                    vkDestroyImageView(LogicalDevice, reinterpret_cast<VkImageView>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::IMAGE:
                    vkDestroyImage(LogicalDevice, reinterpret_cast<VkImage>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::RENDERPASS:
                    vkDestroyRenderPass(LogicalDevice, reinterpret_cast<VkRenderPass>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::BUFFERMEMORY:
                    vkFreeMemory(LogicalDevice, reinterpret_cast<VkDeviceMemory>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::BUFFER:
                    vkDestroyBuffer(LogicalDevice, reinterpret_cast<VkBuffer>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::PIPELINE_LAYOUT:
                    vkDestroyPipelineLayout(LogicalDevice, reinterpret_cast<VkPipelineLayout>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::PIPELINE:
                    vkDestroyPipeline(LogicalDevice, reinterpret_cast<VkPipeline>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT:
                    vkDestroyDescriptorSetLayout(LogicalDevice, reinterpret_cast<VkDescriptorSetLayout>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::DESCRIPTORPOOL:
                    vkDestroyDescriptorPool(LogicalDevice, reinterpret_cast<VkDescriptorPool>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::SEMAPHORE:
                    vkDestroySemaphore(LogicalDevice, reinterpret_cast<VkSemaphore>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::FENCE:
                    vkDestroyFence(LogicalDevice, reinterpret_cast<VkFence>(res_handle.Handle), nullptr);
                    break;
                case Rendering::DeviceResourceType::DESCRIPTORSET:
                {
                    auto ds = reinterpret_cast<VkDescriptorSet>(res_handle.Handle);
                    vkFreeDescriptorSets(LogicalDevice, reinterpret_cast<VkDescriptorPool>(res_handle.Data1), 1, &ds);
                }
                case DeviceResourceType::RESOURCE_COUNT:
                    break;
            }

            DirtyResources.Remove(handle);
        }
    }

    void VulkanDevice::__cleanupBufferDirtyResource()
    {
        size_t dirty_buffer_count = DirtyBuffers.Head();
        for (size_t i = 0; i < dirty_buffer_count; ++i)
        {
            auto handle = DirtyBuffers.ToHandle(i);

            if (!handle)
            {
                continue;
            }

            BufferView& buffer = DirtyBuffers[handle];
            vmaDestroyBuffer(VmaAllocatorValue, buffer.Handle, buffer.Allocation);
            DirtyBuffers.Remove(handle);
        }
    }

    void VulkanDevice::__cleanupBufferImageDirtyResource()
    {
        size_t dirty_buffer_image_count = DirtyBufferImages.Head();
        for (size_t i = 0; i < dirty_buffer_image_count; ++i)
        {
            auto handle = DirtyBufferImages.ToHandle(i);

            if (!handle)
            {
                continue;
            }

            BufferImage& buffer = DirtyBufferImages[handle];

            vkDestroyImageView(LogicalDevice, buffer.ViewHandle, nullptr);
            vkDestroySampler(LogicalDevice, buffer.Sampler, nullptr);
            vmaDestroyImage(VmaAllocatorValue, buffer.Handle, buffer.Allocation);

            DirtyBufferImages.Remove(handle);
        }
    }

    void VulkanDevice::MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data)
    {
        void* mapped_memory;
        if (data)
        {
            ZENGINE_VALIDATE_ASSERT(vmaMapMemory(VmaAllocatorValue, buffer.Allocation, &mapped_memory) == VK_SUCCESS, "Failed to map memory")
            ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(mapped_memory, data_size, data, data_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            vmaUnmapMemory(VmaAllocatorValue, buffer.Allocation);
        }
    }

    BufferView VulkanDevice::CreateBuffer(VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage, VmaAllocationCreateFlags vma_create_flags)
    {
        BufferView         buffer_view                 = {};
        VkBufferCreateInfo buffer_create_info          = {};
        buffer_create_info.sType                       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size                        = byte_size;
        buffer_create_info.usage                       = buffer_usage;
        buffer_create_info.sharingMode                 = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocation_create_info.flags                   = vma_create_flags;

        ZENGINE_VALIDATE_ASSERT(vmaCreateBuffer(VmaAllocatorValue, &buffer_create_info, &allocation_create_info, &(buffer_view.Handle), &(buffer_view.Allocation), nullptr) == VK_SUCCESS, "Failed to create buffer");

        /*
         * Zeroing the buffer
         */
        VmaAllocationInfo allocation_info = {};
        vmaGetAllocationInfo(VmaAllocatorValue, buffer_view.Allocation, &allocation_info);
        Helpers::secure_memset(allocation_info.pMappedData, 0, byte_size, byte_size);

        // Metadata info
        buffer_view.FrameIndex = CurrentFrameIndex;

        if (buffer_usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
        {
            buffer_view.Type = BufferType::VERTEX;
        }
        else if (buffer_usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        {
            buffer_view.Type = BufferType::INDEX;
        }
        else if (buffer_usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        {
            buffer_view.Type = BufferType::STORAGE;
        }
        else if (buffer_usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        {
            buffer_view.Type = BufferType::INDIRECT;
        }
        else if (buffer_usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        {
            buffer_view.Type = BufferType::UNIFORM;
        }

        return buffer_view;
    }

    void VulkanDevice::CopyBuffer(const BufferView& source, const BufferView& destination, VkDeviceSize byte_size, VkDeviceSize src_buffer_offset, VkDeviceSize dst_buffer_offset)
    {
        auto                  command_buffer = GetInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);

        VkBufferMemoryBarrier bufMemBarrier  = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        bufMemBarrier.srcAccessMask          = VK_ACCESS_HOST_WRITE_BIT;
        bufMemBarrier.dstAccessMask          = VK_ACCESS_TRANSFER_READ_BIT;
        bufMemBarrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
        bufMemBarrier.buffer                 = source.Handle;
        bufMemBarrier.offset                 = 0;
        bufMemBarrier.size                   = VK_WHOLE_SIZE;

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

        EnqueueInstantCommandBuffer(command_buffer, dst_pipeline_stage);
    }

    BufferImage VulkanDevice::CreateImage(uint32_t width, uint32_t height, VkImageType image_type, VkImageViewType image_view_type, VkFormat image_format, VkImageTiling image_tiling, VkImageLayout image_initial_layout, VkImageUsageFlags image_usage, VkSharingMode image_sharing_mode, VkSampleCountFlagBits image_sample_count, VkMemoryPropertyFlags requested_properties, VkImageAspectFlagBits image_aspect_flag, uint32_t layer_count, VkImageCreateFlags image_create_flag_bit)
    {
        BufferImage       buffer_image                 = {};
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

        VmaAllocationCreateInfo allocation_create_info = {};
        // allocation_create_info.requiredFlags           = requested_properties;
        allocation_create_info.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocation_create_info.flags                   = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        ZENGINE_VALIDATE_ASSERT(vmaCreateImage(VmaAllocatorValue, &image_create_info, &allocation_create_info, &(buffer_image.Handle), &(buffer_image.Allocation), nullptr) == VK_SUCCESS, "Failed to create buffer");

        buffer_image.ViewHandle = CreateImageView(buffer_image.Handle, image_format, image_view_type, image_aspect_flag, layer_count);
        buffer_image.Sampler    = CreateImageSampler();

        // Metadata info
        buffer_image.FrameIndex = CurrentFrameIndex;

        return buffer_image;
    }

    VkSampler VulkanDevice::CreateImageSampler()
    {
        VkSampler           sampler{VK_NULL_HANDLE};

        VkSamplerCreateInfo sampler_create_info     = {};
        sampler_create_info.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create_info.minFilter               = VK_FILTER_LINEAR;
        sampler_create_info.magFilter               = VK_FILTER_NEAREST;
        sampler_create_info.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_create_info.anisotropyEnable        = PhysicalDeviceFeature.features.samplerAnisotropy;
        sampler_create_info.maxAnisotropy           = PhysicalDeviceFeature.features.samplerAnisotropy ? PhysicalDeviceProperties.limits.maxSamplerAnisotropy : 1.0f;
        sampler_create_info.borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        sampler_create_info.unnormalizedCoordinates = VK_FALSE;
        sampler_create_info.compareEnable           = VK_FALSE;
        sampler_create_info.compareOp               = VK_COMPARE_OP_ALWAYS;
        sampler_create_info.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_create_info.mipLodBias              = 0.0f;
        sampler_create_info.minLod                  = -1000.0f;
        sampler_create_info.maxLod                  = 1000.0f;

        ZENGINE_VALIDATE_ASSERT(vkCreateSampler(LogicalDevice, &sampler_create_info, nullptr, &sampler) == VK_SUCCESS, "Failed to create Texture Sampler")

        return sampler;
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

    VertexBufferSetHandle VulkanDevice::CreateVertexBufferSet()
    {
        auto handle     = VertexBufferSetManager.Create();
        auto buffer_set = VertexBufferSetManager.Access(handle);
        buffer_set->set.init(Arena, SwapchainImageCount, SwapchainImageCount);

        for (unsigned i = 0; i < SwapchainImageCount; ++i)
        {
            buffer_set->set[i] = ZPushStructCtorArgs(Arena, VertexBuffer, this);
        }

        return handle;
    }

    StorageBufferSetHandle VulkanDevice::CreateStorageBufferSet()
    {
        auto handle = StorageBufferSetManager.Create();
        auto buffer = StorageBufferSetManager.Access(handle);
        buffer->set.init(Arena, SwapchainImageCount, SwapchainImageCount);

        for (unsigned i = 0; i < SwapchainImageCount; ++i)
        {
            buffer->set[i] = ZPushStructCtorArgs(Arena, StorageBuffer, this);
        }
        return handle;
    }

    IndirectBufferSetHandle VulkanDevice::CreateIndirectBufferSet()
    {
        auto handle = IndirectBufferSetManager.Create();
        auto buffer = IndirectBufferSetManager.Access(handle);
        buffer->set.init(Arena, SwapchainImageCount, SwapchainImageCount);

        for (unsigned i = 0; i < SwapchainImageCount; ++i)
        {
            buffer->set[i] = ZPushStructCtorArgs(Arena, IndirectBuffer, this);
        }

        return handle;
    }

    IndexBufferSetHandle VulkanDevice::CreateIndexBufferSet()
    {
        auto handle = IndexBufferSetManager.Create();
        auto buffer = IndexBufferSetManager.Access(handle);
        buffer->set.init(Arena, SwapchainImageCount, SwapchainImageCount);

        for (unsigned i = 0; i < SwapchainImageCount; ++i)
        {
            buffer->set[i] = ZPushStructCtorArgs(Arena, IndexBuffer, this);
        }

        return handle;
    }

    UniformBufferSetHandle VulkanDevice::CreateUniformBufferSet()
    {
        auto handle = UniformBufferSetManager.Create();
        auto buffer = UniformBufferSetManager.Access(handle);
        buffer->set.init(Arena, SwapchainImageCount, SwapchainImageCount);

        for (unsigned i = 0; i < SwapchainImageCount; ++i)
        {
            buffer->set[i] = ZPushStructCtorArgs(Arena, UniformBuffer, this);
        }

        return handle;
    }

    void VulkanDevice::CreateSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysicalDevice, Surface, &capabilities);
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            SwapchainImageWidth  = capabilities.currentExtent.width;
            SwapchainImageHeight = capabilities.currentExtent.height;
        }

        auto                     min_image_count       = std::clamp(capabilities.minImageCount, capabilities.minImageCount, capabilities.maxImageCount == 0 ? capabilities.minImageCount + 1 : capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR swapchain_create_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .pNext = nullptr, .surface = Surface, .minImageCount = min_image_count, .imageFormat = SurfaceFormat.format, .imageColorSpace = SurfaceFormat.colorSpace, .imageExtent = VkExtent2D{.width = SwapchainImageWidth, .height = SwapchainImageHeight},
                                .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = PresentMode, .clipped = VK_TRUE
        };

        auto            scratch             = ZGetScratch(Arena);

        Array<uint32_t> family_indice       = {};
        uint32_t        family_indice_count = HasSeperateTransfertQueueFamily ? 2 : 1;
        family_indice.init(scratch.Arena, family_indice_count, family_indice_count);
        family_indice[0] = GraphicFamilyIndex;
        if (HasSeperateTransfertQueueFamily)
        {
            family_indice[1] = TransferFamilyIndex;
        }
        swapchain_create_info.imageSharingMode      = HasSeperateTransfertQueueFamily ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = HasSeperateTransfertQueueFamily ? 2 : 1;
        swapchain_create_info.pQueueFamilyIndices   = family_indice.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateSwapchainKHR(LogicalDevice, &swapchain_create_info, nullptr, &SwapchainHandle) == VK_SUCCESS, "Failed to create Swapchain")

        ZReleaseScratch(scratch);

        uint32_t image_count = 0;
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(LogicalDevice, SwapchainHandle, &image_count, nullptr) == VK_SUCCESS, "Failed to get Images count from Swapchain")

        if (image_count != SwapchainImageCount)
        {
            ZENGINE_CORE_WARN("Max Swapchain image count supported is {}, but requested {}", image_count, SwapchainImageCount);
            SwapchainImageCount = image_count;
            ZENGINE_CORE_WARN("Swapchain image count has changed from {} to {}", SwapchainImageCount, image_count);
        }

        if (SwapchainImageViews.capacity() <= 0)
        {
            SwapchainImageViews.init(Arena, SwapchainImageCount, SwapchainImageCount);
        }

        if (SwapchainFramebuffers.capacity() <= 0)
        {
            SwapchainFramebuffers.init(Arena, SwapchainImageCount, SwapchainImageCount);
        }

        scratch                        = ZGetScratch(Arena);

        Array<VkImage> SwapchainImages = {};
        SwapchainImages.init(scratch.Arena, SwapchainImageCount, SwapchainImageCount);
        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(LogicalDevice, SwapchainHandle, &SwapchainImageCount, SwapchainImages.data()) == VK_SUCCESS, "Failed to get VkImages from Swapchain")

        /*Transition Image from Undefined to Present_src*/
        auto command_buffer = GetInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);
        {
            for (int i = 0; i < SwapchainImages.size(); ++i)
            {
                Rendering::Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                barrier_spec.ImageHandle                                                = SwapchainImages[i];
                barrier_spec.OldLayout                                                  = Specifications::ImageLayout::UNDEFINED;
                barrier_spec.NewLayout                                                  = Specifications::ImageLayout::PRESENT_SRC;
                barrier_spec.ImageAspectMask                                            = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier_spec.SourceAccessMask                                           = 0;
                barrier_spec.DestinationAccessMask                                      = VK_ACCESS_MEMORY_READ_BIT;
                barrier_spec.SourceStageMask                                            = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                barrier_spec.DestinationStageMask                                       = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                barrier_spec.LayerCount                                                 = 1;

                Rendering::Primitives::ImageMemoryBarrier barrier{barrier_spec};
                command_buffer->TransitionImageLayout(barrier);
            }
        }
        EnqueueInstantCommandBuffer(command_buffer);

        for (int i = 0; i < SwapchainImageCount; ++i)
        {
            SwapchainImageViews[i] = CreateImageView(SwapchainImages[i], SurfaceFormat.format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

            Array<VkImageView> fb_images_views;
            fb_images_views.init(scratch.Arena, 1);
            fb_images_views.push(SwapchainImageViews[i]);
            SwapchainFramebuffers[i] = CreateFramebuffer(ArrayView{fb_images_views}, SwapchainAttachment->GetHandle(), SwapchainImageWidth, SwapchainImageHeight);
        }

        ZReleaseScratch(scratch);
    }

    void VulkanDevice::ResizeSwapchain()
    {
        DisposeSwapchain();

        ZENGINE_DESTROY_VULKAN_HANDLE(Instance, vkDestroySurfaceKHR, Surface, nullptr)
        ZENGINE_VALIDATE_ASSERT(CurrentWindow->CreateSurface(Instance, reinterpret_cast<void**>(&Surface)), "Failed Window Surface from GLFW")

        CreateSwapchain();
    }

    void VulkanDevice::DisposeSwapchain()
    {
        for (VkImageView image_view : SwapchainImageViews)
        {
            if (image_view)
            {
                EnqueueForDeletion(DeviceResourceType::IMAGEVIEW, image_view);
            }
        }

        for (VkFramebuffer framebuffer : SwapchainFramebuffers)
        {
            if (framebuffer)
            {
                EnqueueForDeletion(DeviceResourceType::FRAMEBUFFER, framebuffer);
            }
        }

        // We don't call .clear() because we want to reuse the allocated space
        // SwapchainImageViews.clear();
        // SwapchainFramebuffers.clear();

        ZENGINE_DESTROY_VULKAN_HANDLE(LogicalDevice, vkDestroySwapchainKHR, SwapchainHandle, nullptr)
    }

    void VulkanDevice::NewFrame()
    {
        Primitives::Fence* signal_fence = SwapchainSignalFences[CurrentFrameIndex];
        if (!signal_fence->IsSignaled())
        {
            if (!signal_fence->Wait(UINT64_MAX))
            {
                return;
            }
        }

        signal_fence->Reset();
        Primitives::Semaphore* acquired_semaphore = SwapchainAcquiredSemaphores[CurrentFrameIndex];
        ZENGINE_VALIDATE_ASSERT(acquired_semaphore->GetState() != Primitives::SemaphoreState::Submitted, "")

        VkResult acquire_image_result = vkAcquireNextImageKHR(LogicalDevice, SwapchainHandle, UINT64_MAX, acquired_semaphore->GetHandle(), VK_NULL_HANDLE, &SwapchainImageIndex);
        acquired_semaphore->SetState(Primitives::SemaphoreState::Submitted);

        if (acquire_image_result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            ResizeSwapchain();
        }

        m_buffer_manager.ResetPool(CurrentFrameIndex);
    }

    void VulkanDevice::Present()
    {
        Textures::TextureHandle tex_handle = {};
        if (TextureHandleToUpdates.Pop(tex_handle))
        {
            auto texture = GlobalTextures.Access(tex_handle);

            if (!texture)
            {
                TextureHandleToUpdates.Enqueue(tex_handle);
                return;
            }

            else
            {
                auto        img_buf    = Image2DBufferManager.Access(texture->BufferHandle);
                const auto& image_info = img_buf->GetDescriptorImageInfo();

                auto        scratch    = ZGetScratch(Arena);
                {
                    Array<VkWriteDescriptorSet> write_descriptor_sets = {};
                    write_descriptor_sets.init(scratch.Arena, WriteBindlessDescriptorSetRequests.size());

                    for (auto& req : WriteBindlessDescriptorSetRequests)
                    {
                        write_descriptor_sets.push(VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = req.DstSet, .dstBinding = req.Binding, .dstArrayElement = (uint32_t) tex_handle.Index, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &(image_info), .pBufferInfo = nullptr, .pTexelBufferView = nullptr});
                    }

                    vkUpdateDescriptorSets(LogicalDevice, write_descriptor_sets.size(), write_descriptor_sets.data(), 0, nullptr);
                }
                ZReleaseScratch(scratch);
            }
        }

        Textures::TextureHandle tex_to_dispose = {};
        if (TextureHandleToDispose.Pop(tex_to_dispose))
        {
            auto texture = GlobalTextures.Access(tex_to_dispose);
            if (texture)
            {
                texture->Dispose();
                GlobalTextures.Remove(tex_to_dispose);
            }
        }

        Primitives::Semaphore* acquired_semaphore        = SwapchainAcquiredSemaphores[CurrentFrameIndex];
        Primitives::Semaphore* render_complete_semaphore = SwapchainRenderCompleteSemaphores[CurrentFrameIndex];
        Primitives::Fence*     signal_fence              = SwapchainSignalFences[CurrentFrameIndex];

        auto                   scratch                   = ZGetScratch(Arena);

        Array<VkCommandBuffer> buffer;
        buffer.init(scratch.Arena, EnqueuedCommandbufferIndex);
        for (int i = 0; i < EnqueuedCommandbufferIndex; ++i)
        {
            EnqueuedCommandbuffers[i]->End();
            buffer.push(EnqueuedCommandbuffers[i]->GetHandle());
        }

        ZENGINE_VALIDATE_ASSERT(render_complete_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")
        ZENGINE_VALIDATE_ASSERT(signal_fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")

        VkQueue              queue               = m_queue_map.at(Rendering::QueueType::GRAPHIC_QUEUE);
        VkSemaphore          wait_semaphores[]   = {acquired_semaphore->GetHandle()};
        VkSemaphore          signal_semaphores[] = {render_complete_semaphore->GetHandle()};
        VkPipelineStageFlags stage_flags[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info         = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = nullptr, .waitSemaphoreCount = 1, .pWaitSemaphores = wait_semaphores, .pWaitDstStageMask = stage_flags, .commandBufferCount = (uint32_t) buffer.size(), .pCommandBuffers = buffer.data(), .signalSemaphoreCount = 1, .pSignalSemaphores = signal_semaphores};

        auto                 submit              = vkQueueSubmit(queue, 1, &(submit_info), signal_fence->GetHandle());
        ZENGINE_VALIDATE_ASSERT(submit == VK_SUCCESS, "Failed to submit queue")

        ZReleaseScratch(scratch);

        for (int i = 0; i < EnqueuedCommandbufferIndex; ++i)
        {
            EnqueuedCommandbuffers[i]->SetState(CommanBufferState::Pending);
        }

        signal_fence->SetState(FenceState::Submitted);
        render_complete_semaphore->SetState(SemaphoreState::Submitted);

        VkSwapchainKHR   swapchains[]   = {SwapchainHandle};
        uint32_t         frames[]       = {SwapchainImageIndex};
        VkPresentInfoKHR present_info   = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .pNext = nullptr, .waitSemaphoreCount = 1, .pWaitSemaphores = signal_semaphores, .swapchainCount = 1, .pSwapchains = swapchains, .pImageIndices = frames};

        VkResult         present_result = vkQueuePresentKHR(queue, &present_info);
        EnqueuedCommandbufferIndex      = 0;
        acquired_semaphore->SetState(SemaphoreState::Idle);
        render_complete_semaphore->SetState(SemaphoreState::Idle);

        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
        {
            ResizeSwapchain();
            IncrementFrameImageCount();
            return;
        }

        ZENGINE_VALIDATE_ASSERT(present_result == VK_SUCCESS, "Failed to present current frame on Window")

        IncrementFrameImageCount();

        {
            std::lock_guard l(DirtyMutex);
            IdleFrameCount++;
        }
        DirtyCollectorCond.notify_one();
    }

    void VulkanDevice::IncrementFrameImageCount()
    {
        PreviousFrameIndex = CurrentFrameIndex;
        CurrentFrameIndex  = (CurrentFrameIndex + 1) % SwapchainImageCount;
    }

    CommandBuffer* VulkanDevice::GetCommandBuffer(bool begin)
    {
        return m_buffer_manager.GetCommandBuffer(CurrentFrameIndex, begin);
    }

    CommandBuffer* VulkanDevice::GetInstantCommandBuffer(Rendering::QueueType type, bool begin)
    {
        return m_buffer_manager.GetInstantCommandBuffer(type, CurrentFrameIndex, begin);
    }

    void VulkanDevice::EnqueueInstantCommandBuffer(CommandBuffer* const buffer, int wait_flag)
    {
        m_buffer_manager.EndInstantCommandBuffer(buffer, this, wait_flag);
    }

    void VulkanDevice::EnqueueCommandBuffer(CommandBuffer* const buffer)
    {
        EnqueuedCommandbuffers[EnqueuedCommandbufferIndex++] = buffer;
    }

    void VulkanDevice::DirtyCollector()
    {
        using namespace std::chrono_literals;

        ZENGINE_CORE_INFO("[*] Dirty Resource Collector started...")

        while (RunningDirtyCollector)
        {
            std::unique_lock lock(DirtyMutex);
            DirtyCollectorCond.wait(lock, [this] { return (IdleFrameCount > IdleFrameThreshold) || RunningDirtyCollector.load() == false; });

            if (RunningDirtyCollector == false)
            {
                break;
            }

            if (DirtyResources.CanRemove())
            {
                uint32_t dirty_resource_count = DirtyResources.Head();
                for (uint32_t i = 0; i < dirty_resource_count; ++i)
                {
                    auto handle = DirtyResources.ToHandle(i);

                    if (!handle)
                    {
                        continue;
                    }

                    DirtyResource& res_handle = DirtyResources[handle];
                    if (res_handle.FrameIndex == CurrentFrameIndex)
                    {
                        switch (res_handle.Type)
                        {
                            case Rendering::DeviceResourceType::SAMPLER:
                                vkDestroySampler(LogicalDevice, reinterpret_cast<VkSampler>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::FRAMEBUFFER:
                                vkDestroyFramebuffer(LogicalDevice, reinterpret_cast<VkFramebuffer>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::IMAGEVIEW:
                                vkDestroyImageView(LogicalDevice, reinterpret_cast<VkImageView>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::IMAGE:
                                vkDestroyImage(LogicalDevice, reinterpret_cast<VkImage>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::RENDERPASS:
                                vkDestroyRenderPass(LogicalDevice, reinterpret_cast<VkRenderPass>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::BUFFERMEMORY:
                                vkFreeMemory(LogicalDevice, reinterpret_cast<VkDeviceMemory>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::BUFFER:
                                vkDestroyBuffer(LogicalDevice, reinterpret_cast<VkBuffer>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::PIPELINE_LAYOUT:
                                vkDestroyPipelineLayout(LogicalDevice, reinterpret_cast<VkPipelineLayout>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::PIPELINE:
                                vkDestroyPipeline(LogicalDevice, reinterpret_cast<VkPipeline>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::DESCRIPTORSETLAYOUT:
                                vkDestroyDescriptorSetLayout(LogicalDevice, reinterpret_cast<VkDescriptorSetLayout>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::DESCRIPTORPOOL:
                                vkDestroyDescriptorPool(LogicalDevice, reinterpret_cast<VkDescriptorPool>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::SEMAPHORE:
                                vkDestroySemaphore(LogicalDevice, reinterpret_cast<VkSemaphore>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::FENCE:
                                vkDestroyFence(LogicalDevice, reinterpret_cast<VkFence>(res_handle.Handle), nullptr);
                                break;
                            case Rendering::DeviceResourceType::DESCRIPTORSET:
                            {
                                auto ds = reinterpret_cast<VkDescriptorSet>(res_handle.Handle);
                                vkFreeDescriptorSets(LogicalDevice, reinterpret_cast<VkDescriptorPool>(res_handle.Data1), 1, &ds);
                                break;
                            }
                        }

                        DirtyResources.Remove(handle);
                    }
                }
            }

            if (DirtyBuffers.CanRemove())
            {
                uint32_t dirty_buffer_count = DirtyBuffers.Head();
                for (uint32_t i = 0; i < dirty_buffer_count; ++i)
                {
                    auto handle = DirtyBuffers.ToHandle(i);

                    if (!handle)
                    {
                        continue;
                    }

                    BufferView& buffer = DirtyBuffers[handle];
                    if (buffer && buffer.FrameIndex == CurrentFrameIndex)
                    {
                        vmaDestroyBuffer(VmaAllocatorValue, buffer.Handle, buffer.Allocation);
                        buffer.Handle     = VK_NULL_HANDLE;
                        buffer.Allocation = VK_NULL_HANDLE;
                        DirtyBuffers.Remove(handle);
                    }
                }
            }

            if (DirtyBufferImages.CanRemove())
            {
                uint32_t dirty_buffer_image_count = DirtyBufferImages.Head();
                for (uint32_t i = 0; i < dirty_buffer_image_count; ++i)
                {
                    auto handle = DirtyBufferImages.ToHandle(i);

                    if (!handle)
                    {
                        continue;
                    }

                    BufferImage& buffer = DirtyBufferImages[handle];

                    if (buffer && buffer.FrameIndex == CurrentFrameIndex)
                    {
                        vkDestroyImageView(LogicalDevice, buffer.ViewHandle, nullptr);
                        vkDestroySampler(LogicalDevice, buffer.Sampler, nullptr);
                        vmaDestroyImage(VmaAllocatorValue, buffer.Handle, buffer.Allocation);
                        buffer.Handle     = VK_NULL_HANDLE;
                        buffer.Allocation = VK_NULL_HANDLE;
                        DirtyBufferImages.Remove(handle);
                    }
                }
            }

            IdleFrameCount = 0;
        }

        ZENGINE_CORE_INFO("[*] Dirty Resource Collector stopped...")
    }

    Helpers::Handle<Rendering::Shaders::Shader> VulkanDevice::CompileShader(Rendering::Specifications::ShaderSpecification& spec)
    {
        const char* base_dir           = "Shaders/Cache/";
        const char* vertex_name_part   = "_vertex.spv";
        const char* fragment_name_part = "_fragment.spv";

        if (ShaderCaches.contains(spec.Name))
        {
            return ShaderCaches.at(spec.Name);
        }

        auto handle = ShaderManager.Create();
        auto shader = ShaderManager.Access(handle);

        if (shader)
        {
            auto vertex_file   = fmt::format("{}{}{}", base_dir, spec.Name, vertex_name_part);
            auto fragment_file = fmt::format("{}{}{}", base_dir, spec.Name, fragment_name_part);

            if (std::filesystem::exists(vertex_file))
            {
                spec.VertexFilename = vertex_file.c_str();
            }

            if (std::filesystem::exists(fragment_file))
            {
                spec.FragmentFilename = fragment_file.c_str();
            }

            shader->Initialize(this, spec);
        }
        return handle;
    }

    /*
     * CommandBufferManager impl
     */
    CommandBuffer::CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool one_time_usage) : Device(device), QueueType(type), m_command_pool(command_pool)
    {
        Device->Arena->CreateSubArena(ZKilo(120), &LocalArena);
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
        command_buffer_allocation_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocation_info.commandBufferCount          = 1;
        command_buffer_allocation_info.commandPool                 = m_command_pool;

        ZENGINE_VALIDATE_ASSERT(vkAllocateCommandBuffers(Device->LogicalDevice, &command_buffer_allocation_info, &m_command_buffer) == VK_SUCCESS, "Failed to allocate command buffer!")
        m_command_buffer_state = CommanBufferState::Idle;
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
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommanBufferState::Idle, "command buffer must be in Idle state")

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(m_command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")

        m_command_buffer_state = CommanBufferState::Recording;
    }

    void CommandBuffer::End()
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer_state == CommanBufferState::Recording, "command buffer must be in Idle state")
        ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(m_command_buffer) == VK_SUCCESS, "Failed to end recording command buffer!")

        m_command_buffer_state = CommanBufferState::Executable;
    }

    bool CommandBuffer::Completed()
    {
        return m_signal_fence ? m_signal_fence->IsSignaled() : false;
    }

    bool CommandBuffer::IsExecutable()
    {
        return m_command_buffer_state == CommanBufferState::Executable;
    }

    bool CommandBuffer::IsRecording()
    {
        return m_command_buffer_state == CommanBufferState::Recording;
    }

    CommanBufferState CommandBuffer::GetState() const
    {
        return CommanBufferState{m_command_buffer_state.load()};
    }

    void CommandBuffer::ResetState()
    {
        m_command_buffer_state = CommanBufferState::Idle;
        m_signal_fence         = {};
        m_signal_semaphore     = {};
    }

    void CommandBuffer::SetState(const CommanBufferState& state)
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

    void CommandBuffer::BeginRenderPass(Rendering::Renderers::RenderPasses::RenderPass* const render_pass, VkFramebuffer framebuffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

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

        vkCmdBeginRenderPass(m_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = width;
        viewport.height     = height;
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;
        vkCmdSetViewport(m_command_buffer, 0, 1, &viewport);

        /*Scissor definition*/
        VkRect2D scissor = {};
        scissor.offset   = {0, 0};
        scissor.extent   = {width, height};
        vkCmdSetScissor(m_command_buffer, 0, 1, &scissor);

        vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pass->Pipeline->Handle);

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

    void CommandBuffer::BindDescriptorSets(uint32_t frame_index)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (auto render_pass = m_active_render_pass)
        {
            auto                   pipeline           = render_pass->Pipeline;
            auto                   pipeline_layout    = pipeline->Layout;
            auto                   shader             = pipeline->Shader;
            auto&                  descriptor_set_map = shader->DescriptorSetMap;

            auto                   scratch            = ZGetScratch(&LocalArena);
            Array<VkDescriptorSet> frame_sets         = {};
            frame_sets.init(scratch.Arena, 5);

            for (const auto& [_, sets] : descriptor_set_map)
            {
                frame_sets.push(sets[frame_index]);
            }

            vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, frame_sets.size(), frame_sets.data(), 0, nullptr);
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

    void CommandBuffer::DrawIndirect(const Hardwares::IndirectBuffer& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer.GetNativeBufferHandle())
        {
            vkCmdDrawIndirect(m_command_buffer, reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()), 0, buffer.CommandCount, sizeof(VkDrawIndirectCommand));
        }
    }

    void CommandBuffer::DrawIndexedIndirect(const Hardwares::IndirectBuffer& buffer, uint32_t count)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (buffer.GetNativeBufferHandle())
        {
            vkCmdDrawIndexedIndirect(m_command_buffer, reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()), 0, count, sizeof(VkDrawIndexedIndirectCommand));
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

    void CommandBuffer::BindVertexBuffer(Hardwares::VertexBuffer& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        void* handle = buffer.GetNativeBufferHandle();

        if (handle)
        {
            VkDeviceSize vertex_offset[1]  = {0};
            VkBuffer     vertex_buffers[1] = {reinterpret_cast<VkBuffer>(handle)};
            vkCmdBindVertexBuffers(m_command_buffer, 0, 1, vertex_buffers, vertex_offset);
        }
    }

    void CommandBuffer::BindIndexBuffer(const Hardwares::IndexBuffer& buffer, VkIndexType type)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer.GetNativeBufferHandle())
        {
            vkCmdBindIndexBuffer(m_command_buffer, reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()), 0, type);
        }
    }

    void CommandBuffer::SetScissor(const VkRect2D& scissor)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        vkCmdSetScissor(m_command_buffer, 0, 1, &scissor);
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

    void CommandBufferManager::Initialize(VulkanDevice* device, uint8_t swapchain_image_count, int thread_count)
    {
        Device                  = device;
        m_total_pool_count      = swapchain_image_count * thread_count;
        TotalCommandBufferCount = m_total_pool_count * MaxBufferPerPool;
        m_instant_fence         = ZPushStructCtorArgs(Device->Arena, Primitives::Fence, device);
        m_instant_semaphore     = ZPushStructCtorArgs(Device->Arena, Primitives::Semaphore, device);

        CommandPools.init(Device->Arena, m_total_pool_count, m_total_pool_count);
        for (int i = 0; i < m_total_pool_count; ++i)
        {
            CommandPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, device, Rendering::QueueType::GRAPHIC_QUEUE);
        }

        CommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
        for (int i = 0; i < TotalCommandBufferCount; ++i)
        {
            int   pool_index  = GetPoolFromIndex(Rendering::QueueType::GRAPHIC_QUEUE, i);
            auto& pool        = CommandPools[pool_index];
            CommandBuffers[i] = ZPushStructCtorArgs(
                Device->Arena,
                CommandBuffer,
                device,
                pool->Handle,
                pool->QueueType,
                /*(i % MaxBufferPerPool) == 0 ? false : true */ false);
        }

        if (Device->HasSeperateTransfertQueueFamily)
        {
            TransferCommandPools.init(Device->Arena, m_total_pool_count, m_total_pool_count);
            for (int i = 0; i < m_total_pool_count; ++i)
            {
                TransferCommandPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, device, Rendering::QueueType::TRANSFER_QUEUE);
            }

            TransferCommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
            for (int i = 0; i < TotalCommandBufferCount; ++i)
            {
                int   pool_index          = GetPoolFromIndex(Rendering::QueueType::TRANSFER_QUEUE, i);
                auto& pool                = TransferCommandPools[pool_index];
                TransferCommandBuffers[i] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, device, pool->Handle, pool->QueueType, true);
            }
        }
    }

    void CommandBufferManager::Deinitialize()
    {
        m_instant_semaphore = nullptr;
        m_instant_fence     = nullptr;
        CommandBuffers.clear();
        TransferCommandBuffers.clear();

        CommandPools.clear();
        TransferCommandPools.clear();
    }

    CommandBuffer* CommandBufferManager::GetCommandBuffer(uint8_t frame_index, bool begin)
    {
        CommandBuffer* buffer = CommandBuffers[frame_index * MaxBufferPerPool];

        if (begin)
        {
            buffer->ResetState();
            buffer->Begin();
        }
        return buffer;
    }

    CommandBuffer* CommandBufferManager::GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, bool begin)
    {
        CommandBuffer*   buffer = (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandBuffers[frame_index] : CommandBuffers[(frame_index * MaxBufferPerPool) + 1];

        std::unique_lock l(m_instant_command_mutex);
        m_cond.wait(l, [this] { return m_instant_semaphore->GetState() == Primitives::SemaphoreState::Idle; });
        m_executing_instant_command = true;

        if (begin)
        {
            buffer->ResetState();
            buffer->Begin();
        }
        return buffer;
    }

    void CommandBufferManager::EndInstantCommandBuffer(CommandBuffer* const buffer, VulkanDevice* const device, int wait_flag)
    {
        buffer->End();

        auto flag = buffer->QueueType == QueueType::GRAPHIC_QUEUE ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;

        if (wait_flag != -1)
        {
            flag = VkPipelineStageFlagBits(wait_flag);
        }

        device->QueueSubmit(flag, buffer, m_instant_semaphore, m_instant_fence);
        {
            std::unique_lock l(m_instant_command_mutex);
            m_executing_instant_command = false;
        }

        m_cond.notify_one();
    }

    Rendering::Pools::CommandPool* CommandBufferManager::GetCommandPool(Rendering::QueueType type, uint8_t frame_index)
    {
        return (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandPools[frame_index] : CommandPools[frame_index];
    }

    int CommandBufferManager::GetPoolFromIndex(Rendering::QueueType type, uint8_t index)
    {
        return index / MaxBufferPerPool;
    }

    void CommandBufferManager::ResetPool(int frame_index)
    {
        vkResetCommandPool(Device->LogicalDevice, CommandPools[frame_index]->Handle, 0);
        if (Device->HasSeperateTransfertQueueFamily)
        {
            vkResetCommandPool(Device->LogicalDevice, TransferCommandPools[frame_index]->Handle, 0);
        }
    }

    BufferView VertexBuffer::CreateBuffer()
    {
        return m_device->CreateBuffer(m_total_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    BufferView StorageBuffer::CreateBuffer()
    {
        return m_device->CreateBuffer(m_total_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    BufferView IndexBuffer::CreateBuffer()
    {
        return m_device->CreateBuffer(m_total_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    BufferView IndirectBuffer::CreateBuffer()
    {
        return m_device->CreateBuffer(static_cast<VkDeviceSize>(m_total_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    BufferView UniformBuffer::CreateBuffer()
    {
        return m_device->CreateBuffer(static_cast<VkDeviceSize>(m_total_size), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }

    void IndirectBuffer::Upload(const VkDrawIndirectCommand* data, size_t byte_size)
    {
        if (byte_size == 0)
        {
            return;
        }

        CommandCount = byte_size / sizeof(VkDrawIndirectCommand);
        IGraphicBuffer::Upload(data, byte_size);
    }

    void IndirectBuffer::Write(const void* data, size_t byte_size)
    {
        IGraphicBuffer::Write(data, byte_size);
        CommandCount = byte_size / sizeof(VkDrawIndirectCommand);
    }

    void IndirectBuffer::CleanUpMemory()
    {
        CommandCount = 0;
        IGraphicBuffer::CleanUpMemory();
    }

    void UniformBuffer::Allocate(uint64_t byte_size, const char* debug_name)
    {
        if (m_total_size < byte_size || (!m_uniform_buffer_mapped))
        {
            IGraphicBuffer::Allocate(byte_size, debug_name);
            m_uniform_buffer_mapped = true;
        }
    }

    void UniformBuffer::CleanUpMemory()
    {
        IGraphicBuffer::CleanUpMemory();
        m_uniform_buffer_mapped = false;
    }

    void Image2DBuffer::Construct(Hardwares::VulkanDevice* device)
    {
        Device = device;
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
        if (this && m_buffer_image)
        {
            Device->EnqueueBufferImageForDeletion(m_buffer_image);
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

    IGraphicBuffer::~IGraphicBuffer()
    {
        CleanUpMemory();
    }

    void IGraphicBuffer::Allocate(uint64_t size, const char* debug_name)
    {
        if (m_total_size < size)
        {
            CleanUpMemory();
            m_current_offset = 0;
            m_total_size     = size;
            Buffer           = CreateBuffer();
            vmaSetAllocationName(m_device->VmaAllocatorValue, Buffer.Allocation, debug_name);
        }
    }

    void IGraphicBuffer::Clear()
    {
        m_current_offset = 0;
        ClearRange(0, m_current_offset, m_total_size);
    }

    void IGraphicBuffer::ClearRange(uint8_t value, uint32_t offset, size_t byte_size)
    {
        if (byte_size == 0)
        {
            ZENGINE_CORE_WARN("{} : Invalid operation, the byte size is zero", __FUNCTION__)
            return;
        }

        if ((offset + byte_size) > m_total_size)
        {
            ZENGINE_CORE_WARN("{} : Invalid operation, range is out of bound", __FUNCTION__)
            return;
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(m_device->VmaAllocatorValue, Buffer.Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocatorValue, Buffer.Allocation, &allocation_info);
            if (allocation_info.pMappedData)
            {
                auto mapped_buf = reinterpret_cast<uint8_t*>(allocation_info.pMappedData);
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memset((mapped_buf + offset), value, allocation_info.size, byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            }
        }
        else
        {
            BufferView        staging_buffer  = m_device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocatorValue, staging_buffer.Allocation, &allocation_info);

            if (allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memset(allocation_info.pMappedData, value, allocation_info.size, byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(m_device->VmaAllocatorValue, staging_buffer.Allocation, 0, byte_size) == VK_SUCCESS, "Failed to flush allocation")
                m_device->CopyBuffer(staging_buffer, Buffer, byte_size, 0u, offset);
            }

            /* Cleanup resource */
            m_device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void IGraphicBuffer::UploadRange(const void* data, uint32_t offset, size_t byte_size)
    {
        if (byte_size == 0)
        {
            // ZENGINE_CORE_WARN("{} : Invalid operation, the byte size is zero", __FUNCTION__)
            return;
        }

        if ((offset + byte_size) > m_total_size)
        {
            // ZENGINE_CORE_WARN("{} : Invalid operation, the buffer is full", __FUNCTION__)
            return;
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(m_device->VmaAllocatorValue, Buffer.Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->VmaAllocatorValue, data, Buffer.Allocation, offset, byte_size) == VK_SUCCESS, "Failed to perform memory copy operation")

            VkAccessFlags        dst_access_mask    = VK_ACCESS_NONE;
            VkPipelineStageFlags dst_pipeline_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            switch (Buffer.Type)
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
            }

            auto                  command_buffer = m_device->GetInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);
            VkBufferMemoryBarrier bufMemBarrier  = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            bufMemBarrier.srcAccessMask          = VK_ACCESS_HOST_WRITE_BIT;
            bufMemBarrier.dstAccessMask          = dst_access_mask;
            bufMemBarrier.srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            bufMemBarrier.dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
            bufMemBarrier.buffer                 = Buffer.Handle;
            bufMemBarrier.offset                 = 0;
            bufMemBarrier.size                   = VK_WHOLE_SIZE;

            // It's important to insert a buffer memory barrier here to ensure writing to the buffer has finished.
            vkCmdPipelineBarrier(command_buffer->GetHandle(), VK_PIPELINE_STAGE_HOST_BIT, dst_pipeline_stage, 0, 0, nullptr, 1, &bufMemBarrier, 0, nullptr);

            m_device->EnqueueInstantCommandBuffer(command_buffer, dst_pipeline_stage);
        }
        else
        {
            BufferView staging_buffer = m_device->CreateBuffer(static_cast<VkDeviceSize>(byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            ZENGINE_VALIDATE_ASSERT(vmaCopyMemoryToAllocation(m_device->VmaAllocatorValue, data, staging_buffer.Allocation, offset, byte_size) == VK_SUCCESS, "Failed to perform memory copy operation")

            m_device->CopyBuffer(staging_buffer, Buffer, byte_size, 0u, offset);

            /* Cleanup resource */
            m_device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void IGraphicBuffer::Upload(const void* data, size_t byte_size)
    {
        UploadRange(data, m_current_offset, byte_size);
        m_current_offset += byte_size;
    }

    void IGraphicBuffer::Write(const void* data, size_t byte_size)
    {
        UploadRange(data, 0, byte_size);
        m_current_offset = byte_size;
    }

    void IGraphicBuffer::CleanUpMemory()
    {
        if (Buffer)
        {
            m_device->EnqueueBufferForDeletion(Buffer);
            Buffer = {};
        }
    }

    void* IGraphicBuffer::GetNativeBufferHandle() const
    {
        return reinterpret_cast<void*>(Buffer.Handle);
    }

    const VkDescriptorBufferInfo& IGraphicBuffer::GetDescriptorBufferInfo()
    {
        BufferInfo.buffer = Buffer.Handle;
        BufferInfo.offset = 0;
        BufferInfo.range  = m_total_size;
        return BufferInfo;
    }
    void IGraphicBuffer::Dispose()
    {
        CleanUpMemory();
    }

    Rendering::Textures::TextureHandle VulkanDevice::CreateTexture(uint32_t width, uint32_t height)
    {
        return CreateTexture(width, height, 255, 255, 255, 255);
    }

    Rendering::Textures::TextureHandle VulkanDevice::CreateTexture(uint32_t width, uint32_t height, float r, float g, float b, float a)
    {
        uint32_t                             byte_per_pixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(Specifications::ImageFormat::R8G8B8A8_SRGB)];

        Specifications::TextureSpecification spec           = {
                      .Width        = width,
                      .Height       = height,
                      .BytePerPixel = byte_per_pixel,
                      .Format       = Specifications::ImageFormat::R8G8B8A8_SRGB,
        };

        auto tex_handle = CreateTexture(spec);

        if (!tex_handle)
        {
            return Rendering::Textures::TextureHandle{};
        }

        auto resource = GlobalTextures.Access(tex_handle);

        if (!resource)
        {
            return Rendering::Textures::TextureHandle{};
        }

        auto                                            img_buf          = Image2DBufferManager.Access(resource->BufferHandle);
        auto                                            image_buf_handle = img_buf->GetHandle();
        auto                                            image_buf_aspect = (resource->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

        auto                                            new_image_layout = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;

        Specifications::ImageMemoryBarrierSpecification barrier_spec_0   = {};
        barrier_spec_0.ImageHandle                                       = image_buf_handle;
        barrier_spec_0.OldLayout                                         = img_buf->Layout;
        barrier_spec_0.NewLayout                                         = new_image_layout;
        barrier_spec_0.ImageAspectMask                                   = VkImageAspectFlagBits(image_buf_aspect);
        barrier_spec_0.SourceAccessMask                                  = VK_ACCESS_NONE;
        barrier_spec_0.DestinationAccessMask                             = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_spec_0.SourceStageMask                                   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        barrier_spec_0.DestinationStageMask                              = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier_spec_0.LayerCount                                        = spec.LayerCount;
        Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};

        auto                           command_buf = GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE);
        command_buf->TransitionImageLayout(barrier_0);

        img_buf->Layout                 = new_image_layout;

        auto                 scratch    = ZGetScratch(Arena);

        size_t               data_size  = width * height * byte_per_pixel;
        Array<unsigned char> image_data = {};

        unsigned char        r_byte     = static_cast<unsigned char>(std::clamp(r * 255.0f, 0.0f, 255.0f));
        unsigned char        g_byte     = static_cast<unsigned char>(std::clamp(g * 255.0f, 0.0f, 255.0f));
        unsigned char        b_byte     = static_cast<unsigned char>(std::clamp(b * 255.0f, 0.0f, 255.0f));
        unsigned char        a_byte     = static_cast<unsigned char>(std::clamp(a * 255.0f, 0.0f, 255.0f));

        for (size_t i = 0; i < data_size; i += byte_per_pixel)
        {
            image_data[i]     = r_byte;
            image_data[i + 1] = g_byte;
            image_data[i + 2] = b_byte;
            image_data[i + 3] = a_byte;
        }

        WriteTextureData(command_buf, tex_handle, image_data.data());

        ZReleaseScratch(scratch);

        new_image_layout                                               = (image_buf_aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
        Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
        barrier_spec_1.ImageHandle                                     = image_buf_handle;
        barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
        barrier_spec_1.NewLayout                                       = new_image_layout;
        barrier_spec_1.ImageAspectMask                                 = image_buf_aspect;
        barrier_spec_1.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_spec_1.DestinationAccessMask                           = VK_ACCESS_SHADER_READ_BIT;
        barrier_spec_1.SourceStageMask                                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier_spec_1.DestinationStageMask                            = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier_spec_1.LayerCount                                      = spec.LayerCount;
        Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};
        command_buf->TransitionImageLayout(barrier_1);

        EnqueueInstantCommandBuffer(command_buf);

        img_buf->Layout = new_image_layout;

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

    void VulkanDevice::WriteTextureData(CommandBufferPtr command_buf, const Rendering::Textures::TextureHandle& handle, const void* data)
    {
        if (!handle.Valid() || !(data) || !(command_buf))
        {
            return;
        }

        auto       resource       = GlobalTextures.Access(handle);
        auto       image_buf      = Image2DBufferManager.Access(resource->BufferHandle);

        BufferView staging_buffer = CreateBuffer(resource->BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
        MapAndCopyToMemory(staging_buffer, resource->BufferSize, data);
        command_buf->CopyBufferToImage(staging_buffer, image_buf->GetBuffer(), resource->Width, resource->Height, resource->Specification.LayerCount, Specifications::ImageLayoutMap[VALUE_FROM_SPEC_MAP(image_buf->Layout)]);
        EnqueueBufferForDeletion(staging_buffer);
    }

    Rendering::Renderers::RenderPasses::RenderPass* VulkanDevice::CreateRenderPass(const Rendering::Specifications::RenderPassSpecification& spec)
    {
        auto pass = ZPushStructCtorArgs(Arena, Rendering::Renderers::RenderPasses::RenderPass);
        pass->Initialize(this, spec);
        return pass;
    }

} // namespace ZEngine::Hardwares
