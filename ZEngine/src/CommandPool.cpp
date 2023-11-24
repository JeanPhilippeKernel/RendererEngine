#include <pch.h>
#include <ZEngineDef.h>
#include <Rendering/Pools/CommandPool.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Pools
{
    CommandPool::CommandPool(Rendering::QueueType type, uint64_t swapchain_identifier, bool present_on_swapchain)
    {
        /* Create CommandPool */
        m_swapchain_identifier                           = swapchain_identifier;
        m_queue_type                                     = type;
        auto                    device                   = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        Hardwares::QueueView    queue_view               = Hardwares::VulkanDevice::GetQueue(type);
        VkCommandPoolCreateInfo command_pool_create_info = {};
        command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        command_pool_create_info.queueFamilyIndex        = queue_view.FamilyIndex;
        ZENGINE_VALIDATE_ASSERT(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create Command Pool")

        /*Create CommandBuffer */
        m_current_command_buffer_index = 0;
        for (int i = 0; i < m_command_buffer_collection.size(); ++i)
        {
            m_command_buffer_collection[i] = CreateRef<Buffers::CommandBuffer>(m_handle, m_queue_type, present_on_swapchain);
        }
    }

    CommandPool::~CommandPool()
    {
        Hardwares::VulkanDevice::QueueWait(m_queue_type);

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();

        for (Ref<Buffers::CommandBuffer> command : m_command_buffer_collection)
        {
            command->WaitForExecution();

            auto command_buffer_handle = command->GetHandle();
            vkFreeCommandBuffers(device, m_handle, 1, &command_buffer_handle);
        }

        ZENGINE_DESTROY_VULKAN_HANDLE(device, vkDestroyCommandPool, m_handle, nullptr)

        m_command_buffer_collection.fill(nullptr);
    }

    void CommandPool::Tick()
    {
        if (first)
        {
            first = false;
            return;
        }
        m_current_command_buffer_index++;

        if (m_current_command_buffer_index == 10)
        {
            m_current_command_buffer_index = 0;

            auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
            for (auto& command : m_command_buffer_collection)
            {
                ZENGINE_VALIDATE_ASSERT(command->GetState() != Buffers::CommanBufferState::Recording, "")

                if (command->GetState() == Buffers::CommanBufferState::Submitted)
                {
                    command->WaitForExecution();
                }
            }
            ZENGINE_VALIDATE_ASSERT(vkResetCommandPool(device, m_handle, 0) == VK_SUCCESS, "Failed to reset Command Pool")
        }
    }

    Buffers::CommandBuffer* CommandPool::GetCurrentCommmandBuffer()
    {
        return m_command_buffer_collection[m_current_command_buffer_index].get();
    }

    std::vector<Primitives::Semaphore*> CommandPool::GetAllWaitSemaphoreCollection()
    {
        std::vector<Primitives::Semaphore*> wait_semaphore_collection;

        for (auto command : m_command_buffer_collection)
        {
            ZENGINE_VALIDATE_ASSERT(command->GetState() != Buffers::CommanBufferState::Recording, "")

            auto semaphore = command->GetSignalSemaphore();
            if (semaphore->GetState() == Primitives::SemaphoreState::Submitted)
            {
                wait_semaphore_collection.emplace_back(semaphore);
            }
        }
        return wait_semaphore_collection;
    }

    uint64_t CommandPool::GetSwapchainParent() const
    {
        return m_swapchain_identifier;
    }
} // namespace ZEngine::Rendering::Pools