#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Primitives/Fence.h>

namespace ZEngine::Rendering::Primitives
{
    Fence::Fence(Hardwares::VulkanDevice* const device, bool as_signaled)
    {
        Device                                    = device;
        VkFenceCreateInfo frame_fence_create_info = {};
        frame_fence_create_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (as_signaled)
        {
            frame_fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        }

        ZENGINE_VALIDATE_ASSERT(vkCreateFence(Device->LogicalDevice, &frame_fence_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create Fence")
    }

    Fence::~Fence()
    {
        if (!m_handle)
        {
            return;
        }

        Device->EnqueueForDeletion(Rendering::DeviceResourceType::FENCE, m_handle);
        m_handle      = VK_NULL_HANDLE;
        m_fence_state = FenceState::Idle;
    }

    bool Fence::IsSignaled()
    {
        return vkGetFenceStatus(Device->LogicalDevice, m_handle) == VK_SUCCESS;
    }

    bool Fence::Wait(uint64_t timeout)
    {
        auto wait = vkWaitForFences(Device->LogicalDevice, 1, &m_handle, VK_TRUE, timeout);
        return wait == VK_SUCCESS;
    }

    void Fence::Reset()
    {
        ZENGINE_VALIDATE_ASSERT(vkResetFences(Device->LogicalDevice, 1, &m_handle) == VK_SUCCESS, "Failed to reset Fence")
        m_fence_state = FenceState::Idle;
    }

    void Fence::SetState(FenceState state)
    {
        m_fence_state = state;
    }

    FenceState Fence::GetState() const
    {
        return m_fence_state;
    }

    VkFence Fence::GetHandle() const
    {
        return m_handle;
    }
} // namespace ZEngine::Rendering::Primitives
