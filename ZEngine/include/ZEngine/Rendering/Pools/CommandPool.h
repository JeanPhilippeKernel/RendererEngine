#pragma once
#include <ZEngineDef.h>
#include <array>
#include <vulkan/vulkan.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/ResourceTypes.h>

namespace ZEngine::Rendering::Pools
{
    struct CommandPool : public Helpers::RefCounted
    {
        CommandPool(Rendering::QueueType type, uint64_t swapchain_identifier = 0, bool present_on_swapchain = true);
        ~CommandPool();

        void                                Tick();
        Buffers::CommandBuffer*             GetCurrentCommmandBuffer();
        std::vector<Primitives::Semaphore*> GetAllWaitSemaphoreCollection();
        uint64_t                            GetSwapchainParent() const;

    private:
        bool                                        first = true;
        uint64_t                                    m_swapchain_identifier{0};
        uint32_t                                    m_current_command_buffer_index{0};
        VkCommandPool                               m_handle{VK_NULL_HANDLE};
        Rendering::QueueType                        m_queue_type;
        std::array<Ref<Buffers::CommandBuffer>, 10> m_command_buffer_collection;
    };
}