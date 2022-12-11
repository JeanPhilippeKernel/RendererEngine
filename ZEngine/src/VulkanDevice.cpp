#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Hardwares
{
    VulkanDevice::VulkanDevice(const VkPhysicalDevice& physical_device, const std::vector<const char*>& device_extension_layer, const VkSurfaceKHR* surface)
    {
        m_physical_device = physical_device;
        vkGetPhysicalDeviceProperties(m_physical_device, &m_physical_device_properties);
        vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_physical_device_memory_properties);
        vkGetPhysicalDeviceFeatures(m_physical_device, &m_physical_device_feature);

        if ((m_physical_device_feature.geometryShader == VK_TRUE) && (m_physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
        {
            m_high_performant_device = true;
        }

        uint32_t physical_device_queue_family_count{0};
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &physical_device_queue_family_count, nullptr);

        if (physical_device_queue_family_count > 0)
        {
            m_physical_device_queue_family_collection.resize(physical_device_queue_family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &physical_device_queue_family_count, m_physical_device_queue_family_collection.data());

            for (size_t index = 0; index < physical_device_queue_family_count; ++index)
            {
                if (m_physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    if (surface)
                    {
                        VkBool32 present_support = false;

                        ZENGINE_VALIDATE_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, *surface, &present_support) == VK_SUCCESS,
                            "Failed to get device surface support information")

                        if (present_support)
                        {
                            m_graphic_with_present_queue_family_index_collection.push_back(index);
                        }
                        else
                        {
                            m_graphic_queue_family_index_collection.push_back(index);
                        }
                    }
                }
            }
        }

        std::vector<VkDeviceQueueCreateInfo> device_queue_create_info_collection;
        std::vector<float>                   graphic_queue_priorities;
        std::vector<float>                   graphic_with_present_queue_priorities;

        for (auto graphic_queue_family_index : m_graphic_queue_family_index_collection)
        {
            VkDeviceQueueCreateInfo queue_info = {};
            queue_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;


            graphic_queue_priorities.resize(m_physical_device_queue_family_collection[graphic_queue_family_index].queueCount);
            std::fill(std::begin(graphic_queue_priorities), std::end(graphic_queue_priorities), 0.0f);

            queue_info.pQueuePriorities = graphic_queue_priorities.data();
            queue_info.queueFamilyIndex = graphic_queue_family_index;
            queue_info.queueCount       = m_physical_device_queue_family_collection[graphic_queue_family_index].queueCount;
            queue_info.pNext            = nullptr;

            device_queue_create_info_collection.emplace_back(std::move(queue_info));
        }

        for (auto present_queue_family_index : m_graphic_with_present_queue_family_index_collection)
        {
            VkDeviceQueueCreateInfo queue_info = {};
            queue_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;


            graphic_with_present_queue_priorities.resize(m_physical_device_queue_family_collection[present_queue_family_index].queueCount);
            std::fill(std::begin(graphic_with_present_queue_priorities), std::end(graphic_with_present_queue_priorities), 1.0f);

            queue_info.pQueuePriorities = graphic_with_present_queue_priorities.data();
            queue_info.queueFamilyIndex = present_queue_family_index;
            queue_info.queueCount       = m_physical_device_queue_family_collection[present_queue_family_index].queueCount;
            queue_info.pNext            = nullptr;

            device_queue_create_info_collection.emplace_back(std::move(queue_info));
        }

        VkDeviceCreateInfo device_create_info   = {};
        device_create_info.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount = static_cast<uint32_t>(device_queue_create_info_collection.size());
        device_create_info.pQueueCreateInfos    = device_queue_create_info_collection.data();
        device_create_info.enabledLayerCount    = 0;

        device_create_info.enabledExtensionCount   = static_cast<uint32_t>(device_extension_layer.size());
        device_create_info.ppEnabledExtensionNames = (device_extension_layer.size() > 0) ? device_extension_layer.data() : nullptr;
        device_create_info.ppEnabledLayerNames     = nullptr;
        device_create_info.pEnabledFeatures        = nullptr;
        device_create_info.pNext                   = nullptr;

        ZENGINE_VALIDATE_ASSERT(vkCreateDevice(physical_device, &device_create_info, nullptr, &m_logical_device) == VK_SUCCESS, "Failed to create GPU logical device")

        if (!m_graphic_queue_family_index_collection.empty())
        {
            vkGetDeviceQueue(m_logical_device, m_graphic_queue_family_index_collection[0], 0, &m_graphic_queue);
        }

        if (!m_graphic_with_present_queue_family_index_collection.empty())
        {
            vkGetDeviceQueue(m_logical_device, m_graphic_with_present_queue_family_index_collection[0], 0, &m_graphic_with_present_support_queue);
        }
    }

    VulkanDevice::~VulkanDevice()
    {
        vkDeviceWaitIdle(m_logical_device);
        vkDestroyDevice(m_logical_device, nullptr);
    }

    bool VulkanDevice::IsHighPerformant() const
    {
        return m_high_performant_device;
    }

    VkDevice VulkanDevice::GetNativeDeviceHandle() const
    {
        return m_logical_device;
    }

    VkQueue VulkanDevice::GetCurrentGraphicQueue(bool with_present_support) const
    {
        if (with_present_support)
        {
            assert(m_graphic_with_present_support_queue != VK_NULL_HANDLE);
            return m_graphic_with_present_support_queue;
        }
        assert(m_graphic_queue != VK_NULL_HANDLE);
        return m_graphic_queue;
    }

    std::vector<uint32_t> VulkanDevice::GetGraphicQueueFamilyIndexCollection(bool with_present_support) const
    {
        auto queue_family_indice = std::unordered_set<uint32_t>{std::begin(m_graphic_queue_family_index_collection), std::end(m_graphic_queue_family_index_collection)};
        if (with_present_support)
        {
            queue_family_indice.insert(std::begin(m_graphic_with_present_queue_family_index_collection), std::end(m_graphic_with_present_queue_family_index_collection));
        }
        return std::vector<uint32_t>{std::begin(queue_family_indice), std::end(queue_family_indice)};
    }

    VkPhysicalDevice VulkanDevice::GetNativePhysicalDeviceHandle() const
    {
        return m_physical_device;
    }
} // namespace ZEngine::Hardwares
