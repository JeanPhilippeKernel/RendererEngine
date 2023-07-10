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

    void VulkanInstance::CreateInstance(const std::vector<const char*>& additional_extension_layer_name_collection)
    {
        VkApplicationInfo app_info  = {};
        app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName   = m_application_name.data();
        app_info.pEngineName        = m_application_name.data();
        app_info.apiVersion         = VK_API_VERSION_1_2;
        app_info.engineVersion      = 1;
        app_info.applicationVersion = 1;
        app_info.pNext              = nullptr;

        VkInstanceCreateInfo instance_create_info = {};
        instance_create_info.sType                = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo     = &app_info;
        instance_create_info.pNext                = nullptr;
        instance_create_info.flags                = 0;

        auto layer_properties = m_layer.GetInstanceLayerProperties();

        std::vector<const char*> enabled_layer_name_collection;

#ifdef ENABLE_VULKAN_VALIDATION_LAYER
        std::unordered_set<std::string> validation_layer_name_collection = {"VK_LAYER_LUNARG_api_dump", "VK_LAYER_KHRONOS_validation", "VK_LAYER_LUNARG_monitor", "VK_LAYER_LUNARG_screenshot"};

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
            m_selected_layer_property_collection.push_back(*find_it);
        }
#endif

        std::vector<const char*> enabled_extension_layer_name_collection;

        for (const LayerProperty& layer : m_selected_layer_property_collection)
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
    }

    void VulkanInstance::ConfigureDevices(const VkSurfaceKHR* surface)
    {
        ZENGINE_VALIDATE_ASSERT(m_vulkan_instance != VK_NULL_HANDLE, "A Vulkan Instance must be created first!")

        /*Create devices*/
        uint32_t gpu_device_count{0};
        vkEnumeratePhysicalDevices(m_vulkan_instance, &gpu_device_count, nullptr);

        std::vector<VkPhysicalDevice> physical_device_collection(gpu_device_count);
        vkEnumeratePhysicalDevices(m_vulkan_instance, &gpu_device_count, physical_device_collection.data());

        for (const VkPhysicalDevice& physical_device : physical_device_collection)
        {
            std::vector<const char*> requested_device_enabled_layer_name_collection   = {};
            std::vector<const char*> requested_device_extension_layer_name_collection = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

            for (LayerProperty& layer : m_selected_layer_property_collection)
            {
                m_layer.GetExtensionProperties(layer, &physical_device);

                if (!layer.DeviceExtensionCollection.empty())
                {
                    requested_device_enabled_layer_name_collection.push_back(layer.Properties.layerName);
                    for (const auto& extension_property : layer.DeviceExtensionCollection)
                    {
                        requested_device_extension_layer_name_collection.push_back(extension_property.extensionName);
                    }
                }
            }

            m_device_collection.emplace_back(physical_device, requested_device_enabled_layer_name_collection, requested_device_extension_layer_name_collection, surface);
        }
    }

    const std::vector<VulkanDevice>& VulkanInstance::GetDevices() const
    {
        return m_device_collection;
    }

    int VulkanInstance::GetHighPerformantDeviceIndex() const
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
        return high_performant_device_index;
    }

    const VulkanDevice& VulkanInstance::GetHighPerformantDevice() const
    {
        auto device_index = GetHighPerformantDeviceIndex();
        assert(device_index != -1);

        return m_device_collection.at(device_index);
    }

    VulkanDevice& VulkanInstance::GetHighPerformantDevice()
    {
        auto device_index = GetHighPerformantDeviceIndex();
        assert(device_index != -1);

        return m_device_collection[device_index];
    }

    const VkInstance VulkanInstance::GetNativeHandle() const
    {
        return m_vulkan_instance;
    }

    VkBool32 VulkanInstance::__debugCallback(
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
} // namespace ZEngine::Hardwares
