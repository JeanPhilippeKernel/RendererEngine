#pragma once
#include <Rendering/ResourceTypes.h>
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <deque>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Pools
{
    struct CommandPool : public Helpers::RefCounted
    {
        Hardwares::VulkanDevice* Device = nullptr;

        CommandPool(Hardwares::VulkanDevice* device, Rendering::QueueType type);
        ~CommandPool();

        VkCommandPool        Handle{VK_NULL_HANDLE};
        Rendering::QueueType QueueType;
    };
} // namespace ZEngine::Rendering::Pools