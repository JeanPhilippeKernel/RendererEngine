#include <pch.h>
#include <Logging/LoggerDefinition.h>
#include <Hardwares/VulkanInstance.h>

namespace ZEngine::Hardwares
{
    VulkanInstance::VulkanInstance(std::string_view app_name) : m_application_name(app_name), m_layer() {}

    VulkanInstance::~VulkanInstance()
    {
        if (!m_device_collection.empty())
        {
            m_device_collection.clear();
            m_device_collection.shrink_to_fit();
        }

        if (__destroyDebugMessengerPtr)
        {
            __destroyDebugMessengerPtr(m_vulkan_instance, m_debug_messenger, nullptr);
        }
        vkDestroyInstance(m_vulkan_instance, nullptr);
    }

    void VulkanInstance::CreateInstance()
    {
        VkApplicationInfo app_info = {};

        app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName   = m_application_name.data();
        app_info.pEngineName        = m_application_name.data();
        app_info.apiVersion         = VK_API_VERSION_1_3;
        app_info.engineVersion      = 1;
        app_info.applicationVersion = 1;
        app_info.pNext              = nullptr;


        VkInstanceCreateInfo instance_create_info = {};
        instance_create_info.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo     = &app_info;
        instance_create_info.pNext                = nullptr;
        instance_create_info.flags                = 0;

#ifdef ENABLE_VULKAN_VALIDATION_LAYER
        std::unordered_set<std::string> validation_layer_name_collection = {"VK_LAYER_LUNARG_api_dump"};
#endif

        std::unordered_set<std::string> instance_layer_name_collection;

#ifdef ENABLE_VULKAN_VALIDATION_LAYER
        for (auto layer_name : validation_layer_name_collection)
        {
            instance_layer_name_collection.insert(layer_name);
        }
        // std::copy(std::begin(validation_layer_name_collection), std::end(validation_layer_name_collection), std::back_inserter(instance_layer_name_collection));
#endif

        std::unordered_set<std::string> extension_layer_name_collection = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

        auto layer_properties = m_layer.GetInstanceLayerProperties();
        for (const LayerProperty& layer : layer_properties)
        {
            instance_layer_name_collection.insert(layer.Properties.layerName);
            for (const auto& extension : layer.ExtensionCollection)
            {
                extension_layer_name_collection.insert(extension.extensionName);
            }
        }

        std::vector<const char*> enabled_layer_name;
        std::transform(std::begin(instance_layer_name_collection), std::end(instance_layer_name_collection), std::back_inserter(enabled_layer_name),
            [](std::string_view name) { return name.data(); });
        instance_create_info.enabledLayerCount   = instance_layer_name_collection.size();
        instance_create_info.ppEnabledLayerNames = enabled_layer_name.data();

        std::vector<const char*> enabled_extension_layer_name;
        std::transform(std::begin(extension_layer_name_collection), std::end(extension_layer_name_collection), std::back_inserter(enabled_extension_layer_name),
            [](std::string_view name) { return name.data(); });
        instance_create_info.enabledExtensionCount   = extension_layer_name_collection.size();
        instance_create_info.ppEnabledExtensionNames = enabled_extension_layer_name.data();

        VkResult result = vkCreateInstance(&instance_create_info, nullptr, &m_vulkan_instance);

        if (result == VK_ERROR_INCOMPATIBLE_DRIVER)
        {
        }

        if (result == VK_INCOMPLETE)
        {
        }

        /*Create Message Callback*/
        VkDebugUtilsMessengerCreateInfoEXT messenger_create_info = {};
        messenger_create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_create_info.messageSeverity                    = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                              | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
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
    }

    void VulkanInstance::ConfigureDevices(const VkSurfaceKHR* surface)
    {
        assert(m_vulkan_instance != VK_NULL_HANDLE);

        auto layer_properties = m_layer.GetInstanceLayerProperties();

        /*Create devices*/
        uint32_t gpu_device_count{0};
        vkEnumeratePhysicalDevices(m_vulkan_instance, &gpu_device_count, nullptr);

        std::vector<VkPhysicalDevice> physical_device_collection(gpu_device_count);
        vkEnumeratePhysicalDevices(m_vulkan_instance, &gpu_device_count, physical_device_collection.data());

        for (const VkPhysicalDevice& physical_device : physical_device_collection)
        {
            for (LayerProperty& layer : layer_properties)
            {
                m_layer.GetExtensionProperties(layer, &physical_device);
            }
        }

        std::unordered_set<std::string> device_extension_layer_name_collection = {"VK_KHR_swapchain" /*, "VK_KHR_portability_subset"*/};
        for (LayerProperty& layer : layer_properties)
        {
            for (const auto& device_extension : layer.DeviceExtensionCollection)
            {
                device_extension_layer_name_collection.insert(device_extension.extensionName);
            }
        }

        std::vector<const char*> enabled_device_layer_name_collection;
        std::transform(std::begin(device_extension_layer_name_collection), std::end(device_extension_layer_name_collection),
            std::back_inserter(enabled_device_layer_name_collection), [](std::string_view name) { return name.data(); });
        for (const VkPhysicalDevice& physical_device : physical_device_collection)
        {
            m_device_collection.emplace_back(physical_device, enabled_device_layer_name_collection, surface);
        }
    }

    const std::vector<VulkanDevice>& VulkanInstance::GetDevices() const
    {
        return m_device_collection;
    }

    const VulkanDevice& VulkanInstance::GetHighPerformantDevice() const
    {
        int high_performant_device_index{-1};
        for (size_t index = 0; index < m_device_collection.size(); ++index)
        {
            if (m_device_collection[index].IsHighPerformant())
            {
                high_performant_device_index = index;
                break;
            }
        }

        assert(high_performant_device_index != -1);

        return m_device_collection[high_performant_device_index];
    }

    const VkInstance VulkanInstance::GetNativeHandle() const
    {
        return m_vulkan_instance;
    }

    VkBool32 VulkanInstance::__debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
    {
        ZENGINE_CORE_INFO(pCallbackData->pMessage)
        return VK_FALSE;
    }

} // namespace ZEngine::Hardwares
