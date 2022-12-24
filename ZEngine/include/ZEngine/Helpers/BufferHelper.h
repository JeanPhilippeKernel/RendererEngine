#pragma once
#include <Hardwares/VulkanDevice.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Helpers
{
    void CreateBuffer(Hardwares::VulkanDevice& device, VkBuffer& buffer, VkDeviceMemory& device_memory, VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage,
        const VkPhysicalDeviceMemoryProperties& memory_properties, VkMemoryPropertyFlags requested_properties);
    void CopyBuffer(Hardwares::VulkanDevice& device, VkBuffer& source_buffer, VkBuffer& destination_buffer, VkDeviceSize byte_size);
}