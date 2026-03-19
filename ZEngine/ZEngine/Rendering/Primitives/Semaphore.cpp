#include <Hardwares/VulkanDevice.h>
#include <Rendering/Primitives/Semaphore.h>

namespace ZEngine::Rendering::Primitives
{
    Semaphore::Semaphore(Hardwares::VulkanDevice* const device, bool is_timeline)
    {
        IsTimeline                                     = is_timeline;
        Device                                         = device;
        VkSemaphoreTypeCreateInfo timeline_create_info = {};
        timeline_create_info.sType                     = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timeline_create_info.semaphoreType             = is_timeline ? VK_SEMAPHORE_TYPE_TIMELINE : VK_SEMAPHORE_TYPE_BINARY;
        timeline_create_info.initialValue              = 0;

        VkSemaphoreCreateInfo semaphore_create_info    = {};
        semaphore_create_info.sType                    = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create_info.pNext                    = &timeline_create_info;

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
        if (!IsTimeline)
        {
            return;
        }

        VkSemaphoreWaitInfo wait_info = {};
        wait_info.sType               = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.semaphoreCount      = 1;
        wait_info.pSemaphores         = &m_handle;
        wait_info.pValues             = &value;

        ZENGINE_VALIDATE_ASSERT(vkWaitSemaphores(Device->LogicalDevice, &wait_info, timeout) == VK_SUCCESS, "Failed to wait on Semaphore");
    }

    void Semaphore::Signal(const uint64_t value)
    {
        if (!IsTimeline)
        {
            return;
        }

        VkSemaphoreSignalInfo signal_info = {};
        signal_info.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
        signal_info.semaphore             = m_handle;
        signal_info.value                 = value;

        ZENGINE_VALIDATE_ASSERT(vkSignalSemaphore(Device->LogicalDevice, &signal_info) == VK_SUCCESS, "Failed to signal Semaphore");
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
