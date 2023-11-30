#pragma once
#include <vulkan/vulkan.h>
#include <Helpers/IntrusivePtr.h>

namespace ZEngine::Rendering::Primitives
{
    enum class FenceState
    {
        Idle,
        Submitted,
        Undefined,
    };

    struct Fence : public Helpers::RefCounted
    {
        Fence();
        ~Fence();
        bool IsSignaled();

        bool Wait(uint64_t timeout = 1000000000);

        void Reset();

        void       SetState(FenceState state);
        FenceState GetState() const;

        VkFence GetHandle() const;

    private:
        FenceState m_fence_state{FenceState::Idle};
        VkFence    m_handle{VK_NULL_HANDLE};
    };
}