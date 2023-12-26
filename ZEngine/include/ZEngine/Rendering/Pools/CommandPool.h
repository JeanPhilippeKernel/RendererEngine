#pragma once
#include <ZEngineDef.h>
#include <deque>
#include <vulkan/vulkan.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/ResourceTypes.h>

namespace ZEngine::Rendering::Pools
{
    struct CommandPool : public Helpers::RefCounted
    {
        CommandPool(Rendering::QueueType type, uint64_t swapchain_identifier = 0, bool present_on_swapchain = true);
        ~CommandPool();

        Buffers::CommandBuffer*     GetCommmandBuffer();
        Ref<Buffers::CommandBuffer> GetOneTimeCommmandBuffer();

    private:
        uint32_t                                m_max_command_buffer_count{UINT32_MAX};
        std::deque<Ref<Buffers::CommandBuffer>> m_allocated_command_buffers;
        VkCommandPool                           m_handle{VK_NULL_HANDLE};
        Rendering::QueueType                    m_queue_type;
    };
}