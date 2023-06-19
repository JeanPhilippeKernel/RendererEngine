#pragma once
#include <vulkan/vulkan.h>

namespace ZEngine::Hardwares
{
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
        VkQueue                                 GetCurrentGraphicQueue(bool with_present_support = false) const;
        VkQueue                                 GetCurrentTransferQueue() const;
        std::vector<uint32_t>                   GetGraphicQueueFamilyIndexCollection(bool with_present_support = false) const;
        std::vector<uint32_t>                   GetTransferQueueFamilyIndexCollection() const;


        VkCommandPool GetTransferCommandPool();

    private:
        bool                                 m_high_performant_device{false};
        VkDevice                             m_logical_device{VK_NULL_HANDLE};
        VkPhysicalDevice                     m_physical_device{VK_NULL_HANDLE};
        VkPhysicalDeviceProperties           m_physical_device_properties;
        VkPhysicalDeviceFeatures             m_physical_device_feature;
        VkPhysicalDeviceMemoryProperties     m_physical_device_memory_properties;
        std::vector<VkQueueFamilyProperties> m_physical_device_queue_family_collection;
        std::vector<uint32_t>                m_graphic_queue_family_index_collection;
        std::vector<uint32_t>                m_graphic_with_present_queue_family_index_collection;
        std::vector<uint32_t>                m_transfer_queue_family_index_collection;
        VkQueue                              m_graphic_queue{VK_NULL_HANDLE};
        VkQueue                              m_graphic_with_present_support_queue{VK_NULL_HANDLE};
        VkQueue                              m_transfer_queue{VK_NULL_HANDLE};

    private:
        VkCommandPool m_transfer_command_pool{VK_NULL_HANDLE};
    };
} // namespace ZEngine::Hardwares
