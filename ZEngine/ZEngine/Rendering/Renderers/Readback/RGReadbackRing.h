#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Primitives/Semaphore.h>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Renderers
{
    /// @brief Receives one completed GPU-to-CPU readback on the render thread.
    /// @details `data` remains valid only for the duration of the callback. The
    /// callback must copy any result that it needs to retain.
    using RGReadbackFn = void (*)(const void* data, size_t size, void* context);

    /// @brief Persistent host-visible staging allocation leased by a GPU timeline.
    struct RGReadbackAllocation
    {
        Core::Memory::BufferView          Buffer        = {};
        VkDeviceSize                      Capacity      = 0;
        Rendering::Primitives::Semaphore* Timeline      = nullptr;
        uint64_t                          TimelineValue = 0;
        VkDeviceSize                      DataSize      = 0;
        RGReadbackFn                      Callback      = nullptr;
        void*                             Context       = nullptr;
        bool                              Reserved      = false;
        bool                              Submitted     = false;
    };

    /// @brief Reuses mapped GPU-to-CPU staging buffers after their exact timeline completes.
    /// @details Render-thread only. Allocations are not recycled based on frame count: each
    /// one remains reserved through the submission that writes it, then through callback
    /// delivery after the associated timeline reaches `TimelineValue`.
    struct RGReadbackRing
    {
        void                                          Initialize(Hardwares::VulkanDevice* device);
        void                                          Dispose();

        /// @brief Reserves a mapped transfer-destination buffer of at least `size` bytes.
        /// @return A stable allocation index, or UINT32_MAX when no allocation can be made.
        uint32_t                                      Acquire(VkDeviceSize size);
        /// @brief Associates one reserved allocation with the successful copy submission.
        /// @return True if the lease was recorded.
        bool                                          Submit(uint32_t allocation_index, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value, VkDeviceSize data_size, RGReadbackFn callback, void* context);
        /// @brief Releases a reservation whose command buffer will not be submitted.
        void                                          Cancel(uint32_t allocation_index);
        /// @brief Releases all reservations that were not associated with a submission.
        /// @details Call this at the next frame boundary after an aborted graphics submit.
        void                                          CancelUnsubmitted();
        /// @brief Delivers all callbacks whose exact submission timeline has completed.
        void                                          Poll();

        /// @brief Returns the Vulkan buffer for a currently reserved allocation.
        const Core::Memory::BufferView*               GetBuffer(uint32_t allocation_index) const;
        /// @brief Returns whether an allocation has been reserved for this graph frame.
        bool                                          IsReserved(uint32_t allocation_index) const;

        Core::Containers::Array<RGReadbackAllocation> Allocations = {};

    private:
        Hardwares::VulkanDevice* m_device = nullptr;
    };
} // namespace ZEngine::Rendering::Renderers
