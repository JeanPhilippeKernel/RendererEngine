#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/Readback/RGReadbackRing.h>

namespace ZEngine::Rendering::Renderers
{
    void RGReadbackRing::Initialize(Hardwares::VulkanDevice* device)
    {
        m_device = device;
        Allocations.init(device ? device->Arena : nullptr, 8);
    }

    void RGReadbackRing::Dispose()
    {
        if (!m_device)
            return;

        // RenderGraph is torn down only with the renderer/device teardown path,
        // after outstanding graphics work has been retired. Do not put callbacks
        // on DeferredFreeQueue: readback delivery is tied to its own queue timeline.
        for (auto& allocation : Allocations)
        {
            if (allocation.Buffer)
                m_device->GpuMem.FreeBuffer(allocation.Buffer);
            allocation = {};
        }
        Allocations.clear();
        m_device = nullptr;
    }

    uint32_t RGReadbackRing::Acquire(VkDeviceSize size)
    {
        if (!m_device || size == 0)
            return UINT32_MAX;

        for (uint32_t index = 0; index < Allocations.size(); ++index)
        {
            RGReadbackAllocation& allocation = Allocations[index];
            if (!allocation.Reserved && allocation.Buffer && allocation.Capacity >= size)
            {
                allocation.Reserved = true;
                return index;
            }
        }

        RGReadbackAllocation allocation = {};
        allocation.Buffer               = m_device->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Core::Memory::GpuMemoryDomain::HostReadback, "RenderGraphReadback");
        if (!allocation.Buffer || !allocation.Buffer.MappedData)
        {
            if (allocation.Buffer)
                m_device->GpuMem.FreeBuffer(allocation.Buffer);
            ZENGINE_CORE_ERROR("[RenderGraph] Failed to allocate a mapped readback buffer")
            return UINT32_MAX;
        }
        allocation.Capacity = allocation.Buffer.Size;
        allocation.Reserved = true;
        Allocations.push(allocation);
        return static_cast<uint32_t>(Allocations.size() - 1);
    }

    bool RGReadbackRing::Submit(uint32_t allocation_index, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value, VkDeviceSize data_size, RGReadbackFn callback, void* context)
    {
        if (allocation_index >= Allocations.size() || !timeline || timeline_value == 0 || data_size == 0)
            return false;

        RGReadbackAllocation& allocation = Allocations[allocation_index];
        if (!allocation.Reserved || allocation.Submitted || data_size > allocation.Capacity)
            return false;

        allocation.Timeline      = timeline;
        allocation.TimelineValue = timeline_value;
        allocation.DataSize      = data_size;
        allocation.Callback      = callback;
        allocation.Context       = context;
        allocation.Submitted     = true;
        return true;
    }

    void RGReadbackRing::Cancel(uint32_t allocation_index)
    {
        if (allocation_index >= Allocations.size())
            return;

        RGReadbackAllocation& allocation = Allocations[allocation_index];
        if (!allocation.Reserved || allocation.Submitted)
            return;
        allocation.Reserved = false;
    }

    void RGReadbackRing::CancelUnsubmitted()
    {
        for (uint32_t index = 0; index < Allocations.size(); ++index)
            Cancel(index);
    }

    void RGReadbackRing::Poll()
    {
        if (!m_device || m_device->IsDeviceLost.load(std::memory_order_acquire))
            return;

        for (auto& allocation : Allocations)
        {
            if (!allocation.Reserved || !allocation.Submitted || !allocation.Timeline)
                continue;

            uint64_t       completed = 0;
            const VkResult result    = vkGetSemaphoreCounterValue(m_device->LogicalDevice, allocation.Timeline->GetHandle(), &completed);
            if (m_device->CheckDeviceLost(result, "RenderGraph readback timeline poll"))
                return;
            if (result != VK_SUCCESS)
            {
                ZENGINE_CORE_WARN("[RenderGraph] Failed to poll readback timeline (VkResult {})", static_cast<int32_t>(result))
                continue;
            }
            if (completed < allocation.TimelineValue)
                continue;

            if (!m_device->GpuMem.InvalidateAllocation(allocation.Buffer.Allocation, 0, allocation.DataSize))
            {
                ZENGINE_CORE_ERROR("[RenderGraph] Failed to invalidate completed readback allocation")
                allocation.Timeline      = nullptr;
                allocation.TimelineValue = 0;
                allocation.DataSize      = 0;
                allocation.Callback      = nullptr;
                allocation.Context       = nullptr;
                allocation.Reserved      = false;
                allocation.Submitted     = false;
                continue;
            }

            RGReadbackFn callback    = allocation.Callback;
            void*        context     = allocation.Context;
            const void*  data        = allocation.Buffer.MappedData;
            const size_t size        = static_cast<size_t>(allocation.DataSize);

            allocation.Timeline      = nullptr;
            allocation.TimelineValue = 0;
            allocation.DataSize      = 0;
            allocation.Callback      = nullptr;
            allocation.Context       = nullptr;
            allocation.Reserved      = false;
            allocation.Submitted     = false;

            if (callback)
                callback(data, size, context);
        }
    }

    const Core::Memory::BufferView* RGReadbackRing::GetBuffer(uint32_t allocation_index) const
    {
        if (allocation_index >= Allocations.size())
            return nullptr;
        const RGReadbackAllocation& allocation = Allocations[allocation_index];
        return allocation.Reserved && allocation.Buffer ? &allocation.Buffer : nullptr;
    }

    bool RGReadbackRing::IsReserved(uint32_t allocation_index) const
    {
        return allocation_index < Allocations.size() && Allocations[allocation_index].Reserved;
    }
} // namespace ZEngine::Rendering::Renderers
