#include <pch.h>
#include <ZEngineDef.h>
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

        if ((m_physical_device_feature.geometryShader == VK_TRUE) && (m_physical_device_feature.samplerAnisotropy == VK_TRUE) &&
            (m_physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
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

                        ZENGINE_VALIDATE_ASSERT(
                            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, *surface, &present_support) == VK_SUCCESS,
                            "Failed to get device surface support information")

                        if (present_support)
                        {
                            m_graphic_with_present_queue_family_index_collection.push_back(index);
                        }
                    }
                }
                else if (m_physical_device_queue_family_collection[index].queueFlags & VK_QUEUE_TRANSFER_BIT)
                {
                    m_transfer_queue_family_index_collection.push_back(index);
                }
            }
        }

        std::vector<VkDeviceQueueCreateInfo> device_queue_create_info_collection;
        std::vector<float>                   graphic_queue_priorities;
        std::vector<float>                   graphic_with_present_queue_priorities;
        std::vector<float>                   transfer_queue_priorities;

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

        for (auto transfer_queue_family_index : m_transfer_queue_family_index_collection)
        {
            VkDeviceQueueCreateInfo queue_info = {};
            queue_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

            transfer_queue_priorities.resize(m_physical_device_queue_family_collection[transfer_queue_family_index].queueCount);
            std::fill(std::begin(transfer_queue_priorities), std::end(transfer_queue_priorities), 1.0f);

            queue_info.pQueuePriorities = transfer_queue_priorities.data();
            queue_info.queueFamilyIndex = transfer_queue_family_index;
            queue_info.queueCount       = m_physical_device_queue_family_collection[transfer_queue_family_index].queueCount;
            queue_info.pNext            = nullptr;

            device_queue_create_info_collection.emplace_back(std::move(queue_info));
        }

        VkDeviceCreateInfo device_create_info   = {};
        device_create_info.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount = static_cast<uint32_t>(device_queue_create_info_collection.size());
        device_create_info.pQueueCreateInfos    = device_queue_create_info_collection.data();

        device_create_info.enabledExtensionCount   = static_cast<uint32_t>(device_extension_layer.size());
        device_create_info.ppEnabledExtensionNames = (device_extension_layer.size() > 0) ? device_extension_layer.data() : nullptr;
        auto layer                                 = std::array{"VK_LAYER_KHRONOS_validation"};
        device_create_info.enabledLayerCount       = layer.size();
        device_create_info.ppEnabledLayerNames     = layer.data();
        device_create_info.pEnabledFeatures        = nullptr;
        device_create_info.pNext                   = nullptr;

        ZENGINE_VALIDATE_ASSERT(vkCreateDevice(physical_device, &device_create_info, nullptr, &m_logical_device) == VK_SUCCESS, "Failed to create GPU logical device")

        if (!m_graphic_with_present_queue_family_index_collection.empty())
        {
            for (uint32_t family_index : m_graphic_with_present_queue_family_index_collection)
            {
                const auto&          queue_family_property = m_physical_device_queue_family_collection[family_index];
                std::vector<VkQueue> graphic_queue_collection;
                graphic_queue_collection.resize(queue_family_property.queueCount, VK_NULL_HANDLE);
                for (uint32_t queue_index = 0; queue_index < queue_family_property.queueCount; ++queue_index)
                {
                    vkGetDeviceQueue(m_logical_device, family_index, queue_index, &(graphic_queue_collection[queue_index]));
                }

                m_graphic_with_present_support_queue_collection[family_index] = std::move(graphic_queue_collection);
            }
        }

        if (!m_transfer_queue_family_index_collection.empty())
        {
            for (uint32_t family_index : m_transfer_queue_family_index_collection)
            {
                const auto&          queue_family_property = m_physical_device_queue_family_collection[family_index];
                std::vector<VkQueue> transfer_queue_collection;
                transfer_queue_collection.resize(queue_family_property.queueCount, VK_NULL_HANDLE);
                for (uint32_t queue_index = 0; queue_index < queue_family_property.queueCount; ++queue_index)
                {
                    vkGetDeviceQueue(m_logical_device, family_index, queue_index, &(transfer_queue_collection[queue_index]));
                }

                m_transfer_queue_collection[family_index] = std::move(transfer_queue_collection);
            }
        }
    }

    VulkanDevice::~VulkanDevice()
    {
        vkDeviceWaitIdle(m_logical_device);
        for (auto transfer_command_pool : m_transfer_command_pool_collection)
        {
            vkDestroyCommandPool(m_logical_device, transfer_command_pool.second, nullptr);
        }
        for (auto graphic_command_pool : m_graphic_command_pool_collection)
        {
            vkDestroyCommandPool(m_logical_device, graphic_command_pool.second, nullptr);
        }
        m_transfer_command_pool_collection.clear();
        m_graphic_command_pool_collection.clear();
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

    QueueView VulkanDevice::GetAnyGraphicQueue() const
    {
        ZENGINE_VALIDATE_ASSERT(!m_graphic_with_present_support_queue_collection.empty(), "Graphic Queue with Present support can't be empty")

        uint32_t min = 0, max = 0;
        /*Random selection of the queue family*/
        auto begin            = m_graphic_with_present_support_queue_collection.cbegin();
        auto before_end       = std::prev(m_graphic_with_present_support_queue_collection.end());
        min                   = begin->first;
        max                   = before_end->first;
        uint32_t family_index = min + (rand() % static_cast<int>(max - min + 1));

        /*Random selection of queue*/
        const std::vector<VkQueue>& queue_collection = m_graphic_with_present_support_queue_collection.at(family_index);
        ZENGINE_VALIDATE_ASSERT(!queue_collection.empty(), "Graphic Queue collection can't be empty")
        min                  = 0;
        max                  = queue_collection.size() - 1;
        uint32_t queue_index = min + (rand() % static_cast<int>(max - min + 1));

        return QueueView{.FamilyIndex = family_index, .Queue = queue_collection[queue_index]};
    }

    QueueView VulkanDevice::GetCurrentGraphicQueueWithPresentSupport() const
    {
        ZENGINE_VALIDATE_ASSERT(!m_graphic_with_present_support_queue_collection.empty(), "Graphic Queue with Present support can't be empty")
        auto                        begin            = m_graphic_with_present_support_queue_collection.cbegin();
        const std::vector<VkQueue>& queue_collection = begin->second;
        ZENGINE_VALIDATE_ASSERT(!queue_collection.empty(), "Graphic Queue can't be empty")
        return QueueView{.FamilyIndex = begin->first, .Queue = queue_collection[0]};
    }

    QueueView VulkanDevice::GetAnyTransferQueue() const
    {
        ZENGINE_VALIDATE_ASSERT(!m_transfer_queue_collection.empty(), "Transfer Queue/Family collection can't be empty")
        ZENGINE_VALIDATE_ASSERT(m_transfer_queue_collection.size() > 1, "Transfer Queue/Family collection must have at least 2 items")

        uint32_t min = 0, max = 0;
        /*Random selection of the queue family*/
        auto begin            = std::next(m_transfer_queue_collection.cbegin());
        auto before_end       = std::prev(m_transfer_queue_collection.end());
        min                   = begin->first;
        max                   = before_end->first;
        uint32_t family_index = min + (rand() % static_cast<int>(max - min + 1));

        /*Random selection of queue*/
        const std::vector<VkQueue>& queue_collection = m_transfer_queue_collection.at(family_index);
        ZENGINE_VALIDATE_ASSERT(!queue_collection.empty(), "Transfer Queue collection can't be empty")
        min                                          = 0;
        max                                          = queue_collection.size() - 1;
        uint32_t queue_index                         = min + (rand() % static_cast<int>(max - min + 1));

        return QueueView{.FamilyIndex = family_index, .Queue = queue_collection[queue_index]};
    }

    QueueView VulkanDevice::GetCurrentTransferQueue() const
    {
        ZENGINE_VALIDATE_ASSERT(!m_transfer_queue_collection.empty(), "Transfer Queue/Family collection can't be empty")

        auto                        begin            = m_transfer_queue_collection.cbegin();
        const std::vector<VkQueue>& queue_collection = begin->second;
        ZENGINE_VALIDATE_ASSERT(!queue_collection.empty(), "Transfer Queue can't be empty")
        return QueueView{.FamilyIndex = begin->first, .Queue = queue_collection[0]};
    }

    std::vector<uint32_t> VulkanDevice::GetGraphicQueueFamilyIndexCollection() const
    {
        return m_graphic_with_present_queue_family_index_collection;
    }

    std::vector<uint32_t> VulkanDevice::GetTransferQueueFamilyIndexCollection() const
    {
        return m_transfer_queue_family_index_collection;
    }

    VkCommandPool VulkanDevice::GetTransferCommandPool(uint32_t family_index)
    {
        if (!m_transfer_command_pool_collection.empty())
        {
            return m_transfer_command_pool_collection[family_index];
        }

        if (!m_transfer_queue_family_index_collection.empty())
        {
            for (uint32_t queue_family_index : m_transfer_queue_family_index_collection)
            {
                VkCommandPool           command_pool{VK_NULL_HANDLE};
                VkCommandPoolCreateInfo command_pool_create_info = {};
                command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                command_pool_create_info.queueFamilyIndex        = queue_family_index;

                ZENGINE_VALIDATE_ASSERT(
                    vkCreateCommandPool(m_logical_device, &command_pool_create_info, nullptr, &(command_pool)) == VK_SUCCESS, "Failed to create Transfer Command Pool")
                m_transfer_command_pool_collection[queue_family_index] = command_pool;
            }
        }
        return m_transfer_command_pool_collection[family_index];
    }

    VkCommandPool VulkanDevice::GetGraphicCommandPool(uint32_t family_index)
    {
        if (!m_graphic_command_pool_collection.empty())
        {
            return m_graphic_command_pool_collection[family_index];
        }

        if (!m_graphic_with_present_queue_family_index_collection.empty())
        {
            for (uint32_t queue_family_index : m_graphic_with_present_queue_family_index_collection)
            {
                VkCommandPool           command_pool{VK_NULL_HANDLE};
                VkCommandPoolCreateInfo command_pool_create_info = {};
                command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                command_pool_create_info.queueFamilyIndex        = queue_family_index;

                ZENGINE_VALIDATE_ASSERT(
                    vkCreateCommandPool(m_logical_device, &command_pool_create_info, nullptr, &(command_pool)) == VK_SUCCESS, "Failed to create Graphic Command Pool")
                m_graphic_command_pool_collection[queue_family_index] = command_pool;
            }
        }
        return m_graphic_command_pool_collection[family_index];
    }

    VkPhysicalDevice VulkanDevice::GetNativePhysicalDeviceHandle() const
    {
        return m_physical_device;
    }

    const VkPhysicalDeviceProperties& VulkanDevice::GetPhysicalDeviceProperties() const
    {
        return m_physical_device_properties;
    }

    const VkPhysicalDeviceMemoryProperties& VulkanDevice::GetPhysicalDeviceMemoryProperties() const
    {
        return m_physical_device_memory_properties;
    }
} // namespace ZEngine::Hardwares
