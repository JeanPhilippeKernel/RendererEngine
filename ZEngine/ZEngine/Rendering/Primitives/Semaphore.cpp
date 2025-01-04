#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Primitives/Semaphore.h>

namespace ZEngine::Rendering::Primitives
{
    Semaphore::Semaphore(Hardwares::VulkanDevice* const device)
    {
        Device                                      = device;
        VkSemaphoreCreateInfo semaphore_create_info = {};
        semaphore_create_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ZENGINE_VALIDATE_ASSERT(vkCreateSemaphore(Device->LogicalDevice, &semaphore_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create Semaphore")
    }

    Semaphore::~Semaphore()
    {
        if (!m_handle)
        {
            return;
        }

        /*Todo : register for deletion from device*/
        Device->EnqueueForDeletion(Rendering::DeviceResourceType::SEMAPHORE, m_handle);
        m_handle          = VK_NULL_HANDLE;
        m_semaphore_state = SemaphoreState::Idle;
    }

    void Semaphore::Wait(const uint64_t value, const uint64_t timeout)
    {
        /*No-Op for now, because it's for timeline semaphore*/
    }

    void Semaphore::Signal(const uint64_t value)
    {
        /*No-Op for now, because it's for timeline semaphore*/
    }

    VkSemaphore Semaphore::GetHandle() const
    {
        return m_handle;
    }

    void Semaphore::SetState(SemaphoreState state)
    {
        m_semaphore_state = state;
    }

    SemaphoreState Semaphore::GetState() const
    {
        return m_semaphore_state;
    }
} // namespace ZEngine::Rendering::Primitives