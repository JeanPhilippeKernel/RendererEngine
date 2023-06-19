#include <pch.h>
#include <ZEngineDef.h>
#include <Helpers/BufferHelper.h>

namespace ZEngine::Helpers
{
    void CreateBuffer(Hardwares::VulkanDevice& device, VkBuffer& buffer, VkDeviceMemory& device_memory, VkDeviceSize byte_size, VkBufferUsageFlags buffer_usage,
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        VkMemoryPropertyFlags requested_properties)
    {
        VkBufferCreateInfo buffer_create_info = {};
        buffer_create_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size               = byte_size;
        buffer_create_info.usage              = buffer_usage;
        buffer_create_info.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

        ZENGINE_VALIDATE_ASSERT(vkCreateBuffer(device.GetNativeDeviceHandle(), &buffer_create_info, nullptr, &buffer) == VK_SUCCESS, "Failed to create vertex buffer")

        VkMemoryRequirements memory_requirement;
        vkGetBufferMemoryRequirements(device.GetNativeDeviceHandle(), buffer, &memory_requirement);

        bool     memory_type_index_found{false};
        uint32_t memory_type_index{0};
        for (; memory_type_index < memory_properties.memoryTypeCount; ++memory_type_index)
        {
            if ((memory_requirement.memoryTypeBits & (1 << memory_type_index))
                && (memory_properties.memoryTypes[memory_type_index].propertyFlags & requested_properties) == requested_properties)
            {
                memory_type_index_found = true;
                break;
            }
        }

        if (!memory_type_index_found)
        {
            ZENGINE_CORE_ERROR("Failed to find a valid memory type")
        }

        VkMemoryAllocateInfo memory_allocate_info = {};
        memory_allocate_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize       = memory_requirement.size;
        memory_allocate_info.memoryTypeIndex      = memory_type_index;

        // ToDo : Call of vkAllocateMemory(...) isn't optimal because it is limited by maxMemoryAllocationCount
        // It should be replaced by VulkanMemoryAllocator SDK (see :  https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
        ZENGINE_VALIDATE_ASSERT(
            vkAllocateMemory(device.GetNativeDeviceHandle(), &memory_allocate_info, nullptr, &device_memory) == VK_SUCCESS, "Failed to allocate memory for vertex buffer")
        ZENGINE_VALIDATE_ASSERT(vkBindBufferMemory(device.GetNativeDeviceHandle(), buffer, device_memory, 0) == VK_SUCCESS, "Failed to bind the memory to the vertex buffer")
    }

    void CopyBuffer(Hardwares::VulkanDevice& device, VkBuffer& source_buffer, VkBuffer& destination_buffer, VkDeviceSize byte_size)
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
        command_buffer_allocate_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount          = 1;
        command_buffer_allocate_info.commandPool                 = device.GetTransferCommandPool();

        VkCommandBuffer command_buffer;
        ZENGINE_VALIDATE_ASSERT(vkAllocateCommandBuffers(device.GetNativeDeviceHandle(), &command_buffer_allocate_info, &command_buffer) == VK_SUCCESS, "")

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);

        VkBufferCopy buffer_copy = {};
        buffer_copy.srcOffset    = 0;
        buffer_copy.dstOffset    = 0;
        buffer_copy.size         = byte_size;
        vkCmdCopyBuffer(command_buffer, source_buffer, destination_buffer, 1, &buffer_copy);

        vkEndCommandBuffer(command_buffer);

        VkSubmitInfo submit_info = {};
        submit_info.sType        = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers    = &command_buffer;

        vkQueueSubmit(device.GetCurrentTransferQueue(), 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(device.GetCurrentTransferQueue());

        vkFreeCommandBuffers(device.GetNativeDeviceHandle(), device.GetTransferCommandPool(), 1, &command_buffer);
    }
} // namespace ZEngine::Helpers
