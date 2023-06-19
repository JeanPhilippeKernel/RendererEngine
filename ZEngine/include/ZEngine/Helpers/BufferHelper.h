#pragma once
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Helpers
{
    void            CreateBuffer(Hardwares::VulkanDevice& device, VkBuffer& buffer, VkDeviceMemory& device_memory, VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage,
        const VkPhysicalDeviceMemoryProperties& memory_properties, VkMemoryPropertyFlags requested_properties);
    void            CopyBuffer(Hardwares::VulkanDevice& device, VkBuffer& source_buffer, VkBuffer& destination_buffer, VkDeviceSize byte_size);
    VkCommandBuffer BeginOneTimeCommandBuffer(Hardwares::VulkanDevice& device);
    void            EndOneTimeCommandBuffer(VkCommandBuffer& command_buffer, Hardwares::VulkanDevice& device);
    void            CopyBufferToImage(Hardwares::VulkanDevice& device, VkBuffer source_buffer, VkImage destination_image, uint32_t width, uint32_t height);
}