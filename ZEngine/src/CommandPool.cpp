#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Pools/CommandPool.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Pools
{
    CommandPool::CommandPool(Rendering::QueueType type, uint64_t swapchain_identifier, bool present_on_swapchain)
    {
        /* Create CommandPool */
        m_queue_type                                     = type;
        auto                    device                   = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        Hardwares::QueueView    queue_view               = Hardwares::VulkanDevice::GetQueue(type);
        VkCommandPoolCreateInfo command_pool_create_info = {};
        command_pool_create_info.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.flags                   = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        command_pool_create_info.queueFamilyIndex        = queue_view.FamilyIndex;
        ZENGINE_VALIDATE_ASSERT(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create Command Pool")

        auto& properties           = Hardwares::VulkanDevice::GetPhysicalDeviceProperties();
        m_max_command_buffer_count = properties.limits.maxDrawIndirectCount;
    }

    CommandPool::~CommandPool()
    {
        Hardwares::VulkanDevice::QueueWait(m_queue_type);

        for (auto it = m_allocated_command_buffers.begin(); it != m_allocated_command_buffers.end();)
        {
            auto signal_fence = (*it)->GetSignalFence();

            if (signal_fence && signal_fence->Wait())
            {
                it = m_allocated_command_buffers.erase(it);
            }
            else
            {
                it = std::next(it);
            }
        }

        decltype(m_allocated_command_buffers) allocated_buffer;
        std::swap(allocated_buffer, m_allocated_command_buffers);

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_DESTROY_VULKAN_HANDLE(device, vkDestroyCommandPool, m_handle, nullptr)
    }

    Buffers::CommandBuffer* CommandPool::GetCommmandBuffer()
    {
        Buffers::CommandBuffer* m_available_command_buffer{nullptr};

        bool found = false;
        for (int i = 0; i < m_allocated_command_buffers.size(); ++i)
        {
            if (m_allocated_command_buffers[i]->Completed())
            {
                m_allocated_command_buffers[i]->ResetState();
                m_available_command_buffer = m_allocated_command_buffers[i].get();
                found                      = true;
                break;
            }
        }

        if (!found)
        {
            m_allocated_command_buffers.push_back(CreateRef<Buffers::CommandBuffer>(m_handle, m_queue_type, false));
            m_available_command_buffer = m_allocated_command_buffers.back().get();
        }
        return m_available_command_buffer;
    }

    Ref<Buffers::CommandBuffer> CommandPool::GetOneTimeCommmandBuffer()
    {
        return CreateRef<Buffers::CommandBuffer>(m_handle, m_queue_type, true);
    }
} // namespace ZEngine::Rendering::Pools