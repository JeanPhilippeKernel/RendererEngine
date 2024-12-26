#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Pools/CommandPool.h>
#include <ZEngineDef.h>

using namespace ZEngine::Helpers;
namespace ZEngine::Rendering::Pools
{
    CommandPool::CommandPool(Hardwares::VulkanDevice* device, Rendering::QueueType type)
    {
        Device = device;

        QueueType                                        = type;
        Hardwares::QueueView    queue_view               = device->GetQueue(type);
        VkCommandPoolCreateInfo command_pool_create_info = {};
        command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        command_pool_create_info.queueFamilyIndex        = queue_view.FamilyIndex;
        ZENGINE_VALIDATE_ASSERT(vkCreateCommandPool(device->LogicalDevice, &command_pool_create_info, nullptr, &Handle) == VK_SUCCESS, "Failed to create Command Pool")
    }

    CommandPool::~CommandPool()
    {
        Device->QueueWait(QueueType);

        ZENGINE_DESTROY_VULKAN_HANDLE(Device->LogicalDevice, vkDestroyCommandPool, Handle, nullptr)
    }
} // namespace ZEngine::Rendering::Pools