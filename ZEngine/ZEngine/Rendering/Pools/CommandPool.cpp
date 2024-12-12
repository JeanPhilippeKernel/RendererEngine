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

    // Buffers::CommandBuffer* CommandPool::GetCommmandBuffer()
    //{
    //     Buffers::CommandBuffer* m_available_command_buffer{nullptr};

    //    bool found = false;
    //    for (int i = 0; i < m_allocated_command_buffers.size(); ++i)
    //    {
    //        if (m_allocated_command_buffers[i]->Completed())
    //        {
    //            m_allocated_command_buffers[i]->ResetState();
    //            m_available_command_buffer = m_allocated_command_buffers[i].get();
    //            found                      = true;
    //            break;
    //        }
    //    }

    //    if (!found)
    //    {
    //        m_allocated_command_buffers.push_back(CreateRef<Buffers::CommandBuffer>(m_handle, m_queue_type, false));
    //        m_available_command_buffer = m_allocated_command_buffers.back().get();
    //    }
    //    return m_available_command_buffer;
    //}

    // Ref<Buffers::CommandBuffer> CommandPool::GetOneTimeCommmandBuffer()
    //{
    //     return CreateRef<Buffers::CommandBuffer>(m_handle, m_queue_type, true);
    // }
} // namespace ZEngine::Rendering::Pools