#pragma once
#include <ZEngine/Rendering/ResourceTypes.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <deque>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Pools
{
    struct CommandPool
    {
        Hardwares::VulkanDevice* Device = nullptr;

        CommandPool(Hardwares::VulkanDevice* device, Rendering::QueueType type);
        ~CommandPool();

        VkCommandPool        Handle{VK_NULL_HANDLE};
        Rendering::QueueType QueueType;
    };
} // namespace ZEngine::Rendering::Pools