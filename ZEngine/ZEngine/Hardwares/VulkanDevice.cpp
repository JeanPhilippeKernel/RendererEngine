#include <pch.h>
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
#include <Windows/CoreWindow.h>

using namespace std::chrono_literals;
using namespace ZEngine::Rendering::Primitives;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Buffers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Hardwares
{
    void VulkanDevice::Initialize(const Ref<Windows::CoreWindow>& window)
    {
        CurrentWindow                                   = window.get();

        /*Create Vulkan Instance*/
        VkApplicationInfo          app_info             = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pNext = VK_NULL_HANDLE, .pApplicationName = ApplicationName.data(), .applicationVersion = 1, .pEngineName = EngineName.data(), .engineVersion = 1, .apiVersion = VK_API_VERSION_1_3};

        VkInstanceCreateInfo       instance_create_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pNext = VK_NULL_HANDLE, .flags = 0, .pApplicationInfo = &app_info};

        auto                       layer_properties     = m_layer.GetInstanceLayerProperties();

        std::vector<const char*>   enabled_layer_name_collection;
        std::vector<LayerProperty> selected_layer_property_collection;

#ifdef ENABLE_VULKAN_VALIDATION_LAYER
        std::unordered_set<std::string> validation_layer_name_collection = {"VK_LAYER_LUNARG_api_dump", "VK_LAYER_KHRONOS_validation", "VK_LAYER_LUNARG_monitor", "VK_LAYER_LUNARG_screenshot"};

        for (std::string_view layer_name : validation_layer_name_collection)
        {
            auto find_it = std::find_if(std::begin(layer_properties), std::end(layer_properties), [&](const LayerProperty& layer_property) { return std::string_view(layer_property.Properties.layerName) == layer_name; });
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
            auto find_it = std::find_if(std::begin(layer_properties), std::end(layer_properties), [&](const LayerProperty& layer_property) { return std::string_view(layer_property.Properties.layerName) == layer_name; });
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
                if (std::string_view(extension.extensionName) == "VK_EXT_validation_features" || std::string_view(extension.extensionName) == "VK_EXT_layer_settings")
                    continue;
                else
                    enabled_extension_layer_name_collection.push_back(extension.extensionName);
            }
        }

        auto additional_extension_layer_name_collection = window->GetRequiredExtensionLayers();
        if (!additional_extension_layer_name_collection.empty())
        {
            for (const auto& extension : additional_extension_layer_name_collection)
            {
                enabled_extension_layer_name_collection.push_back(extension.c_str());
            }
        }

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

        std::vector<VkPhysicalDevice> physical_device_collection(gpu_device_count);
        vkEnumeratePhysicalDevices(Instance, &gpu_device_count, physical_device_collection.data());

        for (VkPhysicalDevice physical_device : physical_device_collection)
        {
            VkPhysicalDeviceProperties physical_device_properties;
            VkPhysicalDeviceFeatures   physical_device_feature;
            vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
            vkGetPhysicalDeviceFeatures(physical_device, &physical_device_feature);

            if ((physical_device_feature.geometryShader == VK_TRUE) && (physical_device_feature.samplerAnisotropy == VK_TRUE) && ((physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) || (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)))
            {
                PhysicalDevice           = physical_device;
                PhysicalDeviceProperties = physical_device_properties;
                PhysicalDeviceFeature    = physical_device_feature;
                vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &PhysicalDeviceMemoryProperties);
                break;
            }
        }

        std::vector<const char*> requested_device_enabled_layer_name_collection   = {};
        std::vector<const char*> requested_device_extension_layer_name_collection = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME};

        for (LayerProperty& layer : selected_layer_property_collection)
        {
            m_layer.GetExtensionProperties(layer, &PhysicalDevice);

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
        vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &physical_device_queue_family_count, nullptr);

        std::vector<VkQueueFamilyProperties> physical_device_queue_family_collection;
        physical_device_queue_family_collection.resize(physical_device_queue_family_count);
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

        HasSeperateTransfertQueueFamily                                   = GraphicFamilyIndex != TransferFamilyIndex;

        const float                          queue_prorities[]            = {1.0f};
        auto                                 family_index_collection      = std::set{GraphicFamilyIndex, TransferFamilyIndex};
        std::vector<VkDeviceQueueCreateInfo> queue_create_info_collection = {};
        for (uint32_t queue_family_index : family_index_collection)
        {
            VkDeviceQueueCreateInfo& queue_create_info = queue_create_info_collection.emplace_back();
            queue_create_info.sType                    = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.pQueuePriorities         = queue_prorities;
            queue_create_info.queueFamilyIndex         = queue_family_index;
            queue_create_info.queueCount               = 1;
            queue_create_info.pNext                    = nullptr;
        }
        /*
         * Enabling some features
         */
        PhysicalDeviceFeature.drawIndirectFirstInstance                                            = VK_TRUE;
        PhysicalDeviceFeature.multiDrawIndirect                                                    = VK_TRUE;
        PhysicalDeviceFeature.shaderSampledImageArrayDynamicIndexing                               = VK_TRUE;

        VkPhysicalDeviceDescriptorIndexingFeaturesEXT physical_device_descriptor_indexing_features = {};
        physical_device_descriptor_indexing_features.sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
        physical_device_descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing     = VK_TRUE;
        physical_device_descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind  = VK_TRUE;
        physical_device_descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending     = VK_TRUE;
        physical_device_descriptor_indexing_features.descriptorBindingPartiallyBound               = VK_TRUE;
        physical_device_descriptor_indexing_features.runtimeDescriptorArray                        = VK_TRUE;

        VkPhysicalDeviceFeatures2 device_features_2                                                = {};
        device_features_2.sType                                                                    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        device_features_2.pNext                                                                    = &physical_device_descriptor_indexing_features;
        device_features_2.features                                                                 = PhysicalDeviceFeature;

        VkDeviceCreateInfo device_create_info                                                      = {};
        device_create_info.sType                                                                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount                                                    = queue_create_info_collection.size();
        device_create_info.pQueueCreateInfos                                                       = queue_create_info_collection.data();
        device_create_info.enabledExtensionCount                                                   = static_cast<uint32_t>(requested_device_extension_layer_name_collection.size());
        device_create_info.ppEnabledExtensionNames                                                 = (requested_device_extension_layer_name_collection.size() > 0) ? requested_device_extension_layer_name_collection.data() : nullptr;
        device_create_info.pEnabledFeatures                                                        = nullptr;
        device_create_info.pNext                                                                   = &device_features_2;

        ZENGINE_VALIDATE_ASSERT(vkCreateDevice(PhysicalDevice, &device_create_info, nullptr, &LogicalDevice) == VK_SUCCESS, "Failed to create GPU logical device")

        /*Create Vulkan Graphic Queue*/
        m_queue_map[Rendering::QueueType::GRAPHIC_QUEUE] = VK_NULL_HANDLE;
        vkGetDeviceQueue(LogicalDevice, GraphicFamilyIndex, 0, &(m_queue_map[Rendering::QueueType::GRAPHIC_QUEUE]));

        /*Create Vulkan Transfer Queue*/
        if (HasSeperateTransfertQueueFamily)
        {
            m_queue_map[Rendering::QueueType::TRANSFER_QUEUE] = VK_NULL_HANDLE;
            vkGetDeviceQueue(LogicalDevice, TransferFamilyIndex, 0, &(m_queue_map[Rendering::QueueType::TRANSFER_QUEUE]));
        }

        /* Surface format selection */
        uint32_t                        format_count    = 0;
        std::vector<VkSurfaceFormatKHR> surface_formats = {};
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &format_count, nullptr);
        if (format_count != 0)
        {
            surface_formats.resize(format_count);
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
        uint32_t                      present_mode_count = 0;
        std::vector<VkPresentModeKHR> present_modes      = {};
        vkGetPhysicalDeviceSurfacePresentModesKHR(PhysicalDevice, Surface, &present_mode_count, nullptr);
        if (present_mode_count != 0)
        {
            present_modes.resize(present_mode_count);
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

        /*
         * Creating VMA Allocators
         */
        VmaAllocatorCreateInfo vma_allocator_create_info = {.physicalDevice = PhysicalDevice, .device = LogicalDevice, .instance = Instance, .vulkanApiVersion = VK_API_VERSION_1_3};
        ZENGINE_VALIDATE_ASSERT(vmaCreateAllocator(&vma_allocator_create_info, &VmaAllocator) == VK_SUCCESS, "Failed to create VMA Allocator")

        m_buffer_manager.Initialize(this);
        EnqueuedCommandbuffers.resize(m_buffer_manager.TotalCommandBufferCount);

        /*
         * Creating Swapchain
         */
        Specifications::AttachmentSpecification attachment_specification = {.BindPoint = Specifications::PipelineBindPoint::GRAPHIC};
        attachment_specification.ColorsMap[0]                            = {};
        attachment_specification.ColorsMap[0].Format                     = ImageFormat::FORMAT_FROM_DEVICE;
        attachment_specification.ColorsMap[0].Load                       = LoadOperation::CLEAR;
        attachment_specification.ColorsMap[0].Store                      = StoreOperation::STORE;
        attachment_specification.ColorsMap[0].Initial                    = ImageLayout::UNDEFINED;
        attachment_specification.ColorsMap[0].Final                      = ImageLayout::PRESENT_SRC;
        attachment_specification.ColorsMap[0].ReferenceLayout            = ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
        SwapchainAttachment                                              = CreateRef<Rendering::Renderers::RenderPasses::Attachment>(this, attachment_specification);
        PreviousFrameIndex                                               = 0;
        CurrentFrameIndex                                                = 0;

        SwapchainRenderCompleteSemaphores.resize(SwapchainImageCount);
        SwapchainAcquiredSemaphores.resize(SwapchainImageCount);
        SwapchainSignalFences.resize(SwapchainImageCount);
        for (int i = 0; i < SwapchainImageCount; ++i)
        {
            SwapchainAcquiredSemaphores[i]       = CreateRef<Primitives::Semaphore>(this);
            SwapchainRenderCompleteSemaphores[i] = CreateRef<Primitives::Semaphore>(this);
            SwapchainSignalFences[i]             = CreateRef<Primitives::Fence>(this, true);
        }
        CreateSwapchain();

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

        GlobalTextures->Dispose();

        VertexBufferSetManager.Dispose();
        StorageBufferSetManager.Dispose();
        IndirectBufferSetManager.Dispose();
        IndexBufferSetManager.Dispose();
        UniformBufferSetManager.Dispose();

        ZENGINE_CLEAR_STD_VECTOR(EnqueuedCommandbuffers)
        ZENGINE_CLEAR_STD_VECTOR(SwapchainSignalFences)
        ZENGINE_CLEAR_STD_VECTOR(SwapchainAcquiredSemaphores)
        ZENGINE_CLEAR_STD_VECTOR(SwapchainRenderCompleteSemaphores)

        DisposeSwapchain();
        SwapchainAttachment->Dispose();
        ZENGINE_CLEAR_STD_VECTOR(SwapchainImageViews)
        ZENGINE_CLEAR_STD_VECTOR(SwapchainFramebuffers)

        m_buffer_manager.Deinitialize();

        __cleanupBufferDirtyResource();

        __cleanupBufferImageDirtyResource();

        __cleanupDirtyResource();

        ZENGINE_DESTROY_VULKAN_HANDLE(Instance, vkDestroySurfaceKHR, Surface, nullptr)
    }

    void VulkanDevice::Update()
    {
        Textures::TextureHandle tex_handle = {};
        if (TextureHandleToUpdates.Pop(tex_handle))
        {

            auto& texture = GlobalTextures->Access(tex_handle);

            if (!texture)
            {
                TextureHandleToUpdates.Enqueue(tex_handle);
                return;
            }
            const auto&                       image_info            = texture->ImageBuffer->GetDescriptorImageInfo();
            std::vector<VkWriteDescriptorSet> write_descriptor_sets = {};
            write_descriptor_sets.reserve(WriteBindlessDescriptorSetRequests.size());

            for (auto& req : WriteBindlessDescriptorSetRequests)
            {
                write_descriptor_sets.push_back(VkWriteDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = nullptr, .dstSet = req.DstSet, .dstBinding = req.Binding, .dstArrayElement = (uint32_t) tex_handle.Index, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &(image_info), .pBufferInfo = nullptr, .pTexelBufferView = nullptr});
            }

            vkUpdateDescriptorSets(LogicalDevice, write_descriptor_sets.size(), write_descriptor_sets.data(), 0, nullptr);
        }
    }

    void VulkanDevice::Dispose()
    {
        vmaDestroyAllocator(VmaAllocator);

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
            m_dirty_resources.Add({.FrameIndex = CurrentFrameIndex, .Handle = handle, .Type = resource_type});
        }
    }

    void VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType resource_type, DirtyResource resource)
    {
        if (resource.Handle)
        {
            m_dirty_resources.Add(resource);
        }
    }

    void VulkanDevice::EnqueueBufferForDeletion(BufferView& buffer)
    {
        m_dirty_buffers.Add(buffer);
    }

    void VulkanDevice::EnqueueBufferImageForDeletion(BufferImage& buffer)
    {
        m_dirty_buffer_images.Add(buffer);
    }

    void VulkanDevice::QueueWait(Rendering::QueueType type)
    {
        if (!HasSeperateTransfertQueueFamily)
        {
            type = QueueType::GRAPHIC_QUEUE;
        }
        ZENGINE_VALIDATE_ASSERT(vkQueueWaitIdle(m_queue_map[type]) == VK_SUCCESS, "Failed to wait on queue")
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
        return QueueView{.FamilyIndex = queue_family_index, .Handle = m_queue_map[type]};
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
        size_t dirty_resource_count = m_dirty_resources.Head();
        for (size_t i = 0; i < dirty_resource_count; ++i)
        {
            auto handle = m_dirty_resources.ToHandle(i);

            if (!handle)
            {
                continue;
            }

            DirtyResource& res_handle = m_dirty_resources[handle];
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

            m_dirty_resources.Remove(handle);
        }
    }

    void VulkanDevice::__cleanupBufferDirtyResource()
    {
        size_t dirty_buffer_count = m_dirty_buffers.Head();
        for (size_t i = 0; i < dirty_buffer_count; ++i)
        {
            auto handle = m_dirty_buffers.ToHandle(i);

            if (!handle)
            {
                continue;
            }

            BufferView& buffer = m_dirty_buffers[handle];
            vmaDestroyBuffer(VmaAllocator, buffer.Handle, buffer.Allocation);
            m_dirty_buffers.Remove(handle);
        }
    }

    void VulkanDevice::__cleanupBufferImageDirtyResource()
    {
        size_t dirty_buffer_image_count = m_dirty_buffer_images.Head();
        for (size_t i = 0; i < dirty_buffer_image_count; ++i)
        {
            auto handle = m_dirty_buffer_images.ToHandle(i);

            if (!handle)
            {
                continue;
            }

            BufferImage& buffer = m_dirty_buffer_images[handle];

            vkDestroyImageView(LogicalDevice, buffer.ViewHandle, nullptr);
            vkDestroySampler(LogicalDevice, buffer.Sampler, nullptr);
            vmaDestroyImage(VmaAllocator, buffer.Handle, buffer.Allocation);

            m_dirty_buffer_images.Remove(handle);
        }
    }

    void VulkanDevice::MapAndCopyToMemory(BufferView& buffer, size_t data_size, const void* data)
    {
        void* mapped_memory;
        if (data)
        {
            ZENGINE_VALIDATE_ASSERT(vmaMapMemory(VmaAllocator, buffer.Allocation, &mapped_memory) == VK_SUCCESS, "Failed to map memory")
            ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(mapped_memory, data_size, data, data_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            vmaUnmapMemory(VmaAllocator, buffer.Allocation);
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

        ZENGINE_VALIDATE_ASSERT(vmaCreateBuffer(VmaAllocator, &buffer_create_info, &allocation_create_info, &(buffer_view.Handle), &(buffer_view.Allocation), nullptr) == VK_SUCCESS, "Failed to create buffer");

        // Metadata info
        buffer_view.FrameIndex = CurrentFrameIndex;

        return buffer_view;
    }

    void VulkanDevice::CopyBuffer(const BufferView& source, const BufferView& destination, VkDeviceSize byte_size)
    {
        auto command_buffer = GetInstantCommandBuffer(Rendering::QueueType::TRANSFER_QUEUE);
        {
            VkBufferCopy buffer_copy = {};
            buffer_copy.srcOffset    = 0;
            buffer_copy.dstOffset    = 0;
            buffer_copy.size         = byte_size;

            vkCmdCopyBuffer(command_buffer->GetHandle(), source.Handle, destination.Handle, 1, &buffer_copy);
        }
        EnqueueInstantCommandBuffer(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT);
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

        ZENGINE_VALIDATE_ASSERT(vmaCreateImage(VmaAllocator, &image_create_info, &allocation_create_info, &(buffer_image.Handle), &(buffer_image.Allocation), nullptr) == VK_SUCCESS, "Failed to create buffer");

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
        sampler_create_info.maxAnisotropy           = PhysicalDeviceProperties.limits.maxSamplerAnisotropy;
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

    VkFormat VulkanDevice::FindSupportedFormat(const std::vector<VkFormat>& format_collection, VkImageTiling image_tiling, VkFormatFeatureFlags feature_flags)
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
        return FindSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
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

    VkFramebuffer VulkanDevice::CreateFramebuffer(const std::vector<VkImageView>& attachments, const VkRenderPass& render_pass, uint32_t width, uint32_t height, uint32_t layer_number)
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
        auto handle = VertexBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = VertexBufferSetManager.Access(handle);
            buffer       = CreateRef<VertexBufferSet>(this, SwapchainImageCount);
        }

        return handle;
    }

    StorageBufferSetHandle VulkanDevice::CreateStorageBufferSet()
    {
        auto handle = StorageBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = StorageBufferSetManager.Access(handle);
            buffer       = CreateRef<StorageBufferSet>(this, SwapchainImageCount);
        }
        return handle;
    }

    IndirectBufferSetHandle VulkanDevice::CreateIndirectBufferSet()
    {
        auto handle = IndirectBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = IndirectBufferSetManager.Access(handle);
            buffer       = CreateRef<IndirectBufferSet>(this, SwapchainImageCount);
        }
        return handle;
    }

    IndexBufferSetHandle VulkanDevice::CreateIndexBufferSet()
    {
        auto handle = IndexBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = IndexBufferSetManager.Access(handle);
            buffer       = CreateRef<IndexBufferSet>(this, SwapchainImageCount);
        }
        return handle;
    }

    UniformBufferSetHandle VulkanDevice::CreateUniformBufferSet()
    {
        auto handle = UniformBufferSetManager.Create();
        if (handle)
        {
            auto& buffer = UniformBufferSetManager.Access(handle);
            buffer       = CreateRef<UniformBufferSet>(this, SwapchainImageCount);
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

        auto                     min_image_count       = std::clamp(capabilities.minImageCount, capabilities.minImageCount + 1, capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR swapchain_create_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .pNext = nullptr, .surface = Surface, .minImageCount = min_image_count, .imageFormat = SurfaceFormat.format, .imageColorSpace = SurfaceFormat.colorSpace, .imageExtent = VkExtent2D{.width = SwapchainImageWidth, .height = SwapchainImageHeight},
                                .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = PresentMode, .clipped = VK_TRUE
        };

        std::vector<uint32_t> family_indice = {};
        family_indice.resize(HasSeperateTransfertQueueFamily ? 2 : 1);
        family_indice[0] = GraphicFamilyIndex;
        if (HasSeperateTransfertQueueFamily)
        {
            family_indice[1] = TransferFamilyIndex;
        }
        swapchain_create_info.imageSharingMode      = HasSeperateTransfertQueueFamily ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = HasSeperateTransfertQueueFamily ? 2 : 1;
        swapchain_create_info.pQueueFamilyIndices   = family_indice.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateSwapchainKHR(LogicalDevice, &swapchain_create_info, nullptr, &SwapchainHandle) == VK_SUCCESS, "Failed to create Swapchain")

        ZENGINE_VALIDATE_ASSERT(vkGetSwapchainImagesKHR(LogicalDevice, SwapchainHandle, &SwapchainImageCount, nullptr) == VK_SUCCESS, "Failed to get Images count from Swapchain")

        std::vector<VkImage> SwapchainImages = {};
        SwapchainImages.resize(SwapchainImageCount);
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

        SwapchainImageViews.resize(SwapchainImageCount);
        SwapchainFramebuffers.resize(SwapchainImageCount);

        for (int i = 0; i < SwapchainImageCount; ++i)
        {
            SwapchainImageViews[i]   = CreateImageView(SwapchainImages[i], SurfaceFormat.format, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
            SwapchainFramebuffers[i] = CreateFramebuffer({SwapchainImageViews[i]}, SwapchainAttachment->GetHandle(), SwapchainImageWidth, SwapchainImageHeight);
        }
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

        ZENGINE_CLEAR_STD_VECTOR(SwapchainImageViews)
        ZENGINE_CLEAR_STD_VECTOR(SwapchainFramebuffers)

        ZENGINE_DESTROY_VULKAN_HANDLE(LogicalDevice, vkDestroySwapchainKHR, SwapchainHandle, nullptr)
    }

    void VulkanDevice::NewFrame()
    {
        Primitives::Fence* signal_fence = SwapchainSignalFences[CurrentFrameIndex].get();
        if (!signal_fence->IsSignaled())
        {
            if (!signal_fence->Wait(UINT64_MAX))
            {
                return;
            }
        }

        signal_fence->Reset();
        Primitives::Semaphore* acquired_semaphore = SwapchainAcquiredSemaphores[CurrentFrameIndex].get();
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
        Primitives::Semaphore*       acquired_semaphore        = SwapchainAcquiredSemaphores[CurrentFrameIndex].get();
        Primitives::Semaphore*       render_complete_semaphore = SwapchainRenderCompleteSemaphores[CurrentFrameIndex].get();
        Primitives::Fence*           signal_fence              = SwapchainSignalFences[CurrentFrameIndex].get();

        std::vector<VkCommandBuffer> buffer(EnqueuedCommandbufferIndex);
        for (int i = 0; i < EnqueuedCommandbufferIndex; ++i)
        {
            EnqueuedCommandbuffers[i]->End();
            buffer[i] = EnqueuedCommandbuffers[i]->GetHandle();
        }

        ZENGINE_VALIDATE_ASSERT(render_complete_semaphore->GetState() != Rendering::Primitives::SemaphoreState::Submitted, "Signal semaphore is already in a signaled state.")
        ZENGINE_VALIDATE_ASSERT(signal_fence->GetState() != Rendering::Primitives::FenceState::Submitted, "Signal fence is already in a signaled state.")

        VkQueue              queue               = m_queue_map[Rendering::QueueType::GRAPHIC_QUEUE];
        VkSemaphore          wait_semaphores[]   = {acquired_semaphore->GetHandle()};
        VkSemaphore          signal_semaphores[] = {render_complete_semaphore->GetHandle()};
        VkPipelineStageFlags stage_flags[]       = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info         = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = nullptr, .waitSemaphoreCount = 1, .pWaitSemaphores = wait_semaphores, .pWaitDstStageMask = stage_flags, .commandBufferCount = (uint32_t) buffer.size(), .pCommandBuffers = buffer.data(), .signalSemaphoreCount = 1, .pSignalSemaphores = signal_semaphores};

        auto                 submit              = vkQueueSubmit(queue, 1, &(submit_info), signal_fence->GetHandle());
        ZENGINE_VALIDATE_ASSERT(submit == VK_SUCCESS, "Failed to submit queue")

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

            uint32_t dirty_resource_count = m_dirty_resources.Head();
            for (uint32_t i = 0; i < dirty_resource_count; ++i)
            {
                auto handle = m_dirty_resources.ToHandle(i);

                if (!handle)
                {
                    continue;
                }

                DirtyResource& res_handle = m_dirty_resources[handle];
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

                    m_dirty_resources.Remove(handle);
                }
            }

            uint32_t dirty_buffer_count = m_dirty_buffers.Head();
            for (uint32_t i = 0; i < dirty_buffer_count; ++i)
            {
                auto handle = m_dirty_buffers.ToHandle(i);

                if (!handle)
                {
                    continue;
                }

                BufferView& buffer = m_dirty_buffers[handle];
                if (buffer.FrameIndex == CurrentFrameIndex)
                {
                    vmaDestroyBuffer(VmaAllocator, buffer.Handle, buffer.Allocation);
                    m_dirty_buffers.Remove(handle);
                }
            }

            uint32_t dirty_buffer_image_count = m_dirty_buffer_images.Head();
            for (uint32_t i = 0; i < dirty_buffer_image_count; ++i)
            {
                auto handle = m_dirty_buffer_images.ToHandle(i);

                if (!handle)
                {
                    continue;
                }

                BufferImage& buffer = m_dirty_buffer_images[handle];

                if (buffer.FrameIndex == CurrentFrameIndex)
                {
                    vkDestroyImageView(LogicalDevice, buffer.ViewHandle, nullptr);
                    vkDestroySampler(LogicalDevice, buffer.Sampler, nullptr);
                    vmaDestroyImage(VmaAllocator, buffer.Handle, buffer.Allocation);

                    m_dirty_buffer_images.Remove(handle);
                }
            }

            IdleFrameCount = 0;
        }

        ZENGINE_CORE_INFO("[*] Dirty Resource Collector stopped...")
    }

    /*
     * CommandBufferManager impl
     */
    CommandBuffer::CommandBuffer(Hardwares::VulkanDevice* device, VkCommandPool command_pool, Rendering::QueueType type, bool one_time_usage) : Device(device), QueueType(type), m_command_pool(command_pool)
    {
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
        return m_signal_semaphore.get();
    }

    void CommandBuffer::SetSignalFence(const Ref<Primitives::Fence>& semaphore)
    {
        m_signal_fence = semaphore;
    }

    void CommandBuffer::SetSignalSemaphore(const Ref<Primitives::Semaphore>& semaphore)
    {
        m_signal_semaphore = semaphore;
    }

    Primitives::Fence* CommandBuffer::GetSignalFence()
    {
        return m_signal_fence.get();
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

    void CommandBuffer::BeginRenderPass(const Ref<Renderers::RenderPasses::RenderPass>& render_pass, VkFramebuffer framebuffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        const auto&               render_pass_spec = render_pass->Specification;
        const uint32_t            width            = render_pass->GetRenderAreaWidth();
        const uint32_t            height           = render_pass->GetRenderAreaHeight();

        std::vector<VkClearValue> clear_values     = {};

        if (render_pass_spec.SwapchainAsRenderTarget)
        {
            clear_values.push_back(m_clear_value[0]);
        }
        else
        {
            auto& spec = render_pass->Specification;
            for (const auto& handle : spec.Inputs)
            {
                auto texture = Device->GlobalTextures->Access(handle);
                if (texture->IsDepthTexture)
                {
                    clear_values.push_back(m_clear_value[1]);
                    continue;
                }
                clear_values.push_back(m_clear_value[0]);
            }

            for (const auto& handle : spec.ExternalOutputs)
            {
                auto texture = Device->GlobalTextures->Access(handle);

                if (texture->IsDepthTexture)
                {
                    clear_values.push_back(m_clear_value[1]);
                    continue;
                }
                clear_values.push_back(m_clear_value[0]);
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

        vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render_pass->Pipeline->GetHandle());

        m_active_render_pass = render_pass;
    }

    void CommandBuffer::EndRenderPass()
    {
        if (auto render_pass = m_active_render_pass.lock())
        {
            ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
            vkCmdEndRenderPass(m_command_buffer);
            m_active_render_pass.reset();
        }
    }

    void CommandBuffer::BindDescriptorSets(uint32_t frame_index)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (auto render_pass = m_active_render_pass.lock())
        {
            auto                         pipeline           = render_pass->Pipeline;
            auto                         pipeline_layout    = pipeline->GetPipelineLayout();
            auto                         shader             = pipeline->GetShader();
            const auto&                  descriptor_set_map = shader->GetDescriptorSetMap();

            std::vector<VkDescriptorSet> frame_sets         = {};
            for (auto& [_, sets] : descriptor_set_map)
            {
                frame_sets.emplace_back(sets[frame_index]);
            }

            vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, frame_sets.size(), frame_sets.data(), 0, nullptr);
        }
    }

    void CommandBuffer::BindDescriptorSet(const VkDescriptorSet& descriptor)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        ZENGINE_VALIDATE_ASSERT(descriptor != nullptr, "DescriptorSet can't be null")
        if (auto render_pass = m_active_render_pass.lock())
        {
            auto            pipeline_layout = render_pass->Pipeline->GetPipelineLayout();
            VkDescriptorSet desc_set[1]     = {descriptor};
            vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, desc_set, 0, nullptr);
        }
    }

    void CommandBuffer::DrawIndirect(const Hardwares::IndirectBuffer& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")
        if (buffer.GetNativeBufferHandle())
        {
            vkCmdDrawIndirect(m_command_buffer, reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle()), 0, buffer.GetCommandCount(), sizeof(VkDrawIndirectCommand));
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

    void CommandBuffer::BindVertexBuffer(const Hardwares::VertexBuffer& buffer)
    {
        ZENGINE_VALIDATE_ASSERT(m_command_buffer != nullptr, "Command buffer can't be null")

        if (buffer.GetNativeBufferHandle())
        {
            VkDeviceSize vertex_offset[1]  = {0};
            VkBuffer     vertex_buffers[1] = {reinterpret_cast<VkBuffer>(buffer.GetNativeBufferHandle())};
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

        if (auto render_pass = m_active_render_pass.lock())
        {
            auto pipeline_layout = render_pass->Pipeline->GetPipelineLayout();
            vkCmdPushConstants(m_command_buffer, pipeline_layout, stage_flags, offset, size, data);
        }
    }

    void CommandBufferManager::Initialize(VulkanDevice* device, uint8_t swapchain_image_count, int thread_count)
    {
        Device                  = device;
        m_total_pool_count      = swapchain_image_count * thread_count;
        TotalCommandBufferCount = m_total_pool_count * MaxBufferPerPool;
        m_instant_fence         = CreateRef<Primitives::Fence>(device);
        m_instant_semaphore     = CreateRef<Primitives::Semaphore>(device);

        CommandPools.resize(m_total_pool_count, nullptr);
        for (int i = 0; i < m_total_pool_count; ++i)
        {
            CommandPools[i] = CreateRef<Rendering::Pools::CommandPool>(device, Rendering::QueueType::GRAPHIC_QUEUE);
        }

        CommandBuffers.resize(TotalCommandBufferCount, nullptr);
        for (int i = 0; i < TotalCommandBufferCount; ++i)
        {
            int   pool_index  = GetPoolFromIndex(Rendering::QueueType::GRAPHIC_QUEUE, i);
            auto& pool        = CommandPools.at(pool_index);
            CommandBuffers[i] = CreateRef<CommandBuffer>(device, pool->Handle, pool->QueueType, /*(i % MaxBufferPerPool) == 0 ? false : true */ false);
        }

        if (Device->HasSeperateTransfertQueueFamily)
        {
            TransferCommandPools.resize(m_total_pool_count, nullptr);
            for (int i = 0; i < m_total_pool_count; ++i)
            {
                TransferCommandPools[i] = CreateRef<Rendering::Pools::CommandPool>(device, Rendering::QueueType::TRANSFER_QUEUE);
            }

            TransferCommandBuffers.resize(TotalCommandBufferCount, nullptr);
            for (int i = 0; i < TotalCommandBufferCount; ++i)
            {
                int   pool_index          = GetPoolFromIndex(Rendering::QueueType::TRANSFER_QUEUE, i);
                auto& pool                = TransferCommandPools.at(pool_index);
                TransferCommandBuffers[i] = CreateRef<CommandBuffer>(device, pool->Handle, pool->QueueType, true);
            }
        }
    }

    void CommandBufferManager::Deinitialize()
    {
        m_instant_semaphore.reset();
        m_instant_fence.reset();
        ZENGINE_CLEAR_STD_VECTOR(CommandBuffers)
        ZENGINE_CLEAR_STD_VECTOR(TransferCommandBuffers)

        ZENGINE_CLEAR_STD_VECTOR(CommandPools)
        ZENGINE_CLEAR_STD_VECTOR(TransferCommandPools)
    }

    CommandBuffer* CommandBufferManager::GetCommandBuffer(uint8_t frame_index, bool begin)
    {
        CommandBuffer* buffer = CommandBuffers[frame_index * MaxBufferPerPool].get();

        if (begin)
        {
            buffer->ResetState();
            buffer->Begin();
        }
        return buffer;
    }

    CommandBuffer* CommandBufferManager::GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, bool begin)
    {
        CommandBuffer*   buffer = (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandBuffers[frame_index].get() : CommandBuffers[(frame_index * MaxBufferPerPool) + 1].get();

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
        device->QueueSubmit(flag, buffer, m_instant_semaphore.get(), m_instant_fence.get());
        {
            std::unique_lock l(m_instant_command_mutex);
            m_executing_instant_command = false;
        }

        m_cond.notify_one();
    }

    Rendering::Pools::CommandPool* CommandBufferManager::GetCommandPool(Rendering::QueueType type, uint8_t frame_index)
    {
        return (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandPools[frame_index].get() : CommandPools[frame_index].get();
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

    void VertexBuffer::SetData(const void* data, size_t byte_size)
    {

        if (byte_size == 0)
        {
            return;
        }

        if (this->m_byte_size != byte_size)
        {
            /*
             * Tracking the size change..
             */
            m_last_byte_size = m_byte_size;

            CleanUpMemory();
            this->m_byte_size = byte_size;
            m_vertex_buffer   = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(m_device->VmaAllocator, m_vertex_buffer.Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, m_vertex_buffer.Allocation, &allocation_info);
            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            }
        }
        else
        {
            BufferView        staging_buffer  = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, staging_buffer.Allocation, &allocation_info);

            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(m_device->VmaAllocator, staging_buffer.Allocation, 0, static_cast<VkDeviceSize>(this->m_byte_size)) == VK_SUCCESS, "Failed to flush allocation")
                m_device->CopyBuffer(staging_buffer, m_vertex_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
            }

            /* Cleanup resource */
            m_device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void VertexBuffer::CleanUpMemory()
    {
        if (m_vertex_buffer)
        {
            m_device->EnqueueBufferForDeletion(m_vertex_buffer);
            m_vertex_buffer = {};
        }
    }

    void StorageBuffer::SetData(const void* data, uint32_t offset, size_t byte_size)
    {
        if (byte_size == 0)
        {
            return;
        }

        if (this->m_byte_size < byte_size)
        {
            /*
             * Tracking the size change..
             */
            m_last_byte_size = m_byte_size;

            CleanUpMemory();
            this->m_byte_size = byte_size;
            m_storage_buffer  = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(m_device->VmaAllocator, m_storage_buffer.Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, m_storage_buffer.Allocation, &allocation_info);
            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            }
        }
        else
        {
            BufferView        staging_buffer  = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, staging_buffer.Allocation, &allocation_info);

            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(m_device->VmaAllocator, staging_buffer.Allocation, 0, static_cast<VkDeviceSize>(this->m_byte_size)) == VK_SUCCESS, "Failed to flush allocation")
                m_device->CopyBuffer(staging_buffer, m_storage_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
            }

            /* Cleanup resource */
            m_device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void StorageBuffer::CleanUpMemory()
    {
        if (m_storage_buffer)
        {
            m_device->EnqueueBufferForDeletion(m_storage_buffer);
            m_storage_buffer = {};
        }
    }

    void IndexBuffer::SetData(const void* data, size_t byte_size)
    {

        if (byte_size == 0)
        {
            return;
        }

        if (this->m_byte_size != byte_size)
        {
            /*
             * Tracking the size change..
             */
            m_last_byte_size = m_byte_size;

            CleanUpMemory();
            this->m_byte_size = byte_size;
            m_index_buffer    = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(m_device->VmaAllocator, m_index_buffer.Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, m_index_buffer.Allocation, &allocation_info);
            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            }
        }
        else
        {
            BufferView        staging_buffer  = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, staging_buffer.Allocation, &allocation_info);

            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(m_device->VmaAllocator, staging_buffer.Allocation, 0, static_cast<VkDeviceSize>(this->m_byte_size)) == VK_SUCCESS, "Failed to flush allocation")
                m_device->CopyBuffer(staging_buffer, m_index_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
            }

            /* Cleanup resource */
            m_device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void IndexBuffer::CleanUpMemory()
    {
        if (m_index_buffer)
        {
            m_device->EnqueueBufferForDeletion(m_index_buffer);
            m_index_buffer = {};
        }
    }

    void IndirectBuffer::SetData(const VkDrawIndirectCommand* data, size_t byte_size)
    {
        if (byte_size == 0)
        {
            return;
        }

        if (this->m_byte_size < byte_size)
        {
            /*
             * Tracking the size change..
             */
            m_last_byte_size = m_byte_size;

            CleanUpMemory();
            this->m_byte_size = byte_size;
            m_command_count   = byte_size / sizeof(VkDrawIndirectCommand);
            m_indirect_buffer = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        }

        VkMemoryPropertyFlags mem_prop_flags;
        vmaGetAllocationMemoryProperties(m_device->VmaAllocator, m_indirect_buffer.Allocation, &mem_prop_flags);

        if (mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        {
            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, m_indirect_buffer.Allocation, &allocation_info);
            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
            }
        }
        else
        {
            BufferView        staging_buffer  = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

            VmaAllocationInfo allocation_info = {};
            vmaGetAllocationInfo(m_device->VmaAllocator, staging_buffer.Allocation, &allocation_info);

            if (data && allocation_info.pMappedData)
            {
                ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
                ZENGINE_VALIDATE_ASSERT(vmaFlushAllocation(m_device->VmaAllocator, staging_buffer.Allocation, 0, VK_WHOLE_SIZE) == VK_SUCCESS, "Failed to flush allocation")
                m_device->CopyBuffer(staging_buffer, m_indirect_buffer, static_cast<VkDeviceSize>(this->m_byte_size));
            }

            /* Cleanup resource */
            m_device->EnqueueBufferForDeletion(staging_buffer);
        }
    }

    void IndirectBuffer::CleanUpMemory()
    {
        m_command_count = 0;

        if (m_indirect_buffer)
        {
            m_device->EnqueueBufferForDeletion(m_indirect_buffer);
            m_indirect_buffer = {};
        }
    }

    void UniformBuffer::SetData(const void* data, size_t byte_size)
    {
        if (byte_size == 0)
        {
            return;
        }

        if (this->m_byte_size < byte_size || (!m_uniform_buffer_mapped))
        {
            /*
             * Tracking the size change..
             */
            m_last_byte_size = m_byte_size;

            CleanUpMemory();
            this->m_byte_size       = byte_size;
            m_uniform_buffer        = m_device->CreateBuffer(static_cast<VkDeviceSize>(this->m_byte_size), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
            m_uniform_buffer_mapped = true;
        }

        VmaAllocationInfo allocation_info = {};
        vmaGetAllocationInfo(m_device->VmaAllocator, m_uniform_buffer.Allocation, &allocation_info);

        if (allocation_info.pMappedData)
        {
            ZENGINE_VALIDATE_ASSERT(Helpers::secure_memset(allocation_info.pMappedData, 0, this->m_byte_size, allocation_info.size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory set operation")
        }

        if (data && allocation_info.pMappedData)
        {
            ZENGINE_VALIDATE_ASSERT(Helpers::secure_memcpy(allocation_info.pMappedData, allocation_info.size, data, this->m_byte_size) == Helpers::MEMORY_OP_SUCCESS, "Failed to perform memory copy operation")
        }
    }

    void UniformBuffer::CleanUpMemory()
    {
        if (m_uniform_buffer)
        {
            m_device->EnqueueBufferForDeletion(m_uniform_buffer);

            m_uniform_buffer_mapped = false;
            m_uniform_buffer        = {};
        }
    }

    Image2DBuffer::Image2DBuffer(Hardwares::VulkanDevice* device, const Specifications::Image2DBufferSpecification& spec) : m_device(device), m_width(spec.Width), m_height(spec.Height)
    {
        ZENGINE_VALIDATE_ASSERT(m_width > 0, "Image width must be greater then zero")
        ZENGINE_VALIDATE_ASSERT(m_height > 0, "Image height must be greater then zero")

        Specifications::ImageViewType   image_view_type   = Specifications::ImageViewType::TYPE_2D;
        Specifications::ImageCreateFlag image_create_flag = Specifications::ImageCreateFlag::NONE;

        if (spec.BufferUsageType == Specifications::ImageBufferUsageType::CUBEMAP)
        {
            image_view_type   = Specifications::ImageViewType::TYPE_CUBE;
            image_create_flag = Specifications::ImageCreateFlag::CUBE_COMPATIBLE_BIT;
        }

        m_buffer_image = m_device->CreateImage(m_width, m_height, VK_IMAGE_TYPE_2D, Specifications::ImageViewTypeMap[VALUE_FROM_SPEC_MAP(image_view_type)], spec.ImageFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED, spec.ImageUsage, VK_SHARING_MODE_EXCLUSIVE, VK_SAMPLE_COUNT_1_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, spec.ImageAspectFlag, spec.LayerCount, Specifications::ImageCreateFlagMap[VALUE_FROM_SPEC_MAP(image_create_flag)]);
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
            m_device->EnqueueBufferImageForDeletion(m_buffer_image);
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

} // namespace ZEngine::Hardwares
