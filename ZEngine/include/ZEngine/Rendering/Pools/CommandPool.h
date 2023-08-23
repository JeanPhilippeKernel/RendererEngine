#pragma once
#include <ZEngineDef.h>
#include <array>
#include <vulkan/vulkan.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/ResourceTypes.h>

namespace ZEngine::Rendering::Pools
{
    struct CommandPool
    {
        CommandPool(Rendering::QueueType type, bool present_on_swapchain = true);
        ~CommandPool();

        Buffers::CommandBuffer*             GetCurrentCommmandBuffer();
        std::vector<Primitives::Semaphore*> GetAllWaitSemaphoreCollection();

    private:
        uint32_t                                    m_current_command_buffer_index{0};
        VkCommandPool                               m_handle{VK_NULL_HANDLE};
        Rendering::QueueType                        m_queue_type;
        std::array<Ref<Buffers::CommandBuffer>, 10> m_command_buffer_collection;
    };
}