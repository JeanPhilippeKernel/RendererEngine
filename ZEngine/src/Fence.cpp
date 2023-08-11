#include <pch.h>
#include <Rendering/Primitives/Fence.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Primitives
{
    Fence::Fence()
    {
        VkFenceCreateInfo frame_fence_create_info = {};
        frame_fence_create_info.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_VALIDATE_ASSERT(vkCreateFence(device, &frame_fence_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create Fence")
    }

    Fence::~Fence()
    {
        if (!m_handle)
        {
            return;
        }

        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::FENCE, m_handle);
        m_handle      = VK_NULL_HANDLE;
        m_fence_state = FenceState::Idle;
    }

    bool Fence::IsSignaled()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        return vkGetFenceStatus(device, m_handle) == VK_SUCCESS;
    }

    bool Fence::Wait(uint64_t timeout)
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        return vkWaitForFences(device, 1, &m_handle, true, timeout) == VK_SUCCESS;
    }

    void Fence::Reset()
    {
        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_VALIDATE_ASSERT(vkResetFences(device, 1, &m_handle) == VK_SUCCESS, "Failed to reset Fence")
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
}
