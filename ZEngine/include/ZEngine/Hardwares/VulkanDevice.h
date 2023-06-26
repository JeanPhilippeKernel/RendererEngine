#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>

namespace ZEngine::Hardwares
{
    struct QueueView
    {
        uint32_t FamilyIndex{0xFFFFFFFF};
        VkQueue  Queue{VK_NULL_HANDLE};
    };

    class VulkanDevice
    {
    public:
        VulkanDevice(const VkPhysicalDevice& physical_device, const std::vector<const char*>& device_extension_layer = {}, const VkSurfaceKHR* surface = nullptr);
        ~VulkanDevice();

        bool IsHighPerformant() const;

        VkPhysicalDevice                        GetNativePhysicalDeviceHandle() const;
        const VkPhysicalDeviceProperties&       GetPhysicalDeviceProperties() const;
        const VkPhysicalDeviceMemoryProperties& GetPhysicalDeviceMemoryProperties() const;
        VkDevice                                GetNativeDeviceHandle() const;
        QueueView                               GetAnyGraphicQueue() const;
        QueueView                               GetCurrentGraphicQueueWithPresentSupport() const;
        QueueView                               GetAnyTransferQueue() const;
        QueueView                               GetCurrentTransferQueue() const;
        std::vector<uint32_t>                   GetGraphicQueueFamilyIndexCollection() const;
        std::vector<uint32_t>                   GetTransferQueueFamilyIndexCollection() const;

        VkCommandPool GetTransferCommandPool(uint32_t family_index);
        VkCommandPool GetGraphicCommandPool(uint32_t family_index);

    private:
        bool                                               m_high_performant_device{false};
        VkDevice                                           m_logical_device{VK_NULL_HANDLE};
        VkPhysicalDevice                                   m_physical_device{VK_NULL_HANDLE};
        VkPhysicalDeviceProperties                         m_physical_device_properties;
        VkPhysicalDeviceFeatures                           m_physical_device_feature;
        VkPhysicalDeviceMemoryProperties                   m_physical_device_memory_properties;
        std::vector<VkQueueFamilyProperties>               m_physical_device_queue_family_collection;
        std::vector<uint32_t>                              m_graphic_with_present_queue_family_index_collection;
        std::vector<uint32_t>                              m_transfer_queue_family_index_collection;
        std::unordered_map<uint32_t, std::vector<VkQueue>> m_graphic_with_present_support_queue_collection;
        std::unordered_map<uint32_t, std::vector<VkQueue>> m_transfer_queue_collection;

    private:
        std::unordered_map<uint32_t, VkCommandPool> m_graphic_command_pool_collection;
        std::unordered_map<uint32_t, VkCommandPool> m_transfer_command_pool_collection;
    };
} // namespace ZEngine::Hardwares
