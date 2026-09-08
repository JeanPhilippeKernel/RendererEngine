#include <ZEngine/Hardwares/CommandBufferManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Logging/LoggerDefinition.h>

using namespace ZEngine::Rendering;

namespace ZEngine::Hardwares
{
    void CommandBufferManager::Initialize(VulkanDevice* device, uint32_t image_count, uint8_t override_thread_count)
    {
        if (m_is_initialized)
        {
            ZENGINE_CORE_WARN("Attempted to call {}, but it has been already initialized", __FUNCTION__)
            return;
        }

        Device                         = device;
        TotalThreadCount               = override_thread_count > 0 ? override_thread_count : device->WorkerThreadCount;
        TotalPoolCount                 = image_count * TotalThreadCount;
        TotalCommandBufferCount        = TotalPoolCount * MaxBufferPerPool;
        TotalInstantCommandBufferCount = MaxBufferPerPool * MaxBufferPerPool * TotalPoolCount; // We want to have enough instant command buffers for each pool, so we can guarantee that there will always be an instant command buffer available for each pool when needed

        InstantGraphicsPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
        InstantGraphicsCommandBuffers.init(Device->Arena, TotalInstantCommandBufferCount, TotalInstantCommandBufferCount);
        CommandPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
        CommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
        EnqueuedCommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);

        for (uint32_t i = 0; i < TotalPoolCount; ++i)
        {
            InstantGraphicsPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, QueueType::GRAPHIC_QUEUE);

            for (uint32_t buf_idx = 0; buf_idx < (MaxBufferPerPool * MaxBufferPerPool); ++buf_idx)
            {
                uint32_t buffer_idx                       = (i * (MaxBufferPerPool * MaxBufferPerPool)) + buf_idx;
                InstantGraphicsCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, InstantGraphicsPools[i]->Handle, InstantGraphicsPools[i]->QueueType, true);
            }
        }

        for (uint32_t i = 0; i < TotalPoolCount; ++i)
        {
            CommandPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, QueueType::GRAPHIC_QUEUE);
            for (uint32_t buf_idx = 0; buf_idx < MaxBufferPerPool; ++buf_idx)
            {
                uint32_t buffer_idx        = (i * MaxBufferPerPool) + buf_idx;
                bool     is_primary        = (buffer_idx % 2) == 0;
                CommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, CommandPools[i]->Handle, CommandPools[i]->QueueType, is_primary);
            }
        }

        if (Device->HasSeperateTransfertQueueFamily)
        {
            InstantTransferPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            TransferCommandPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            TransferCommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
            InstantTransferCommandBuffers.init(Device->Arena, TotalInstantCommandBufferCount, TotalInstantCommandBufferCount);

            for (uint32_t i = 0; i < TotalPoolCount; ++i)
            {
                InstantTransferPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::TRANSFER_QUEUE);
                for (uint32_t buf_idx = 0; buf_idx < (MaxBufferPerPool * MaxBufferPerPool); ++buf_idx)
                {
                    uint32_t buffer_idx                       = (i * (MaxBufferPerPool * MaxBufferPerPool)) + buf_idx;
                    InstantTransferCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, InstantTransferPools[i]->Handle, InstantTransferPools[i]->QueueType, true);
                }
            }

            for (uint32_t i = 0; i < TotalPoolCount; ++i)
            {
                TransferCommandPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::TRANSFER_QUEUE);
                for (uint32_t buf_idx = 0; buf_idx < MaxBufferPerPool; ++buf_idx)
                {
                    uint32_t buffer_idx                = (i * MaxBufferPerPool) + buf_idx;
                    TransferCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, TransferCommandPools[i]->Handle, TransferCommandPools[i]->QueueType, true);
                }
            }
        }

        m_is_initialized = true;
    }

    void CommandBufferManager::Deinitialize()
    {
        // Explicitly destroy each CommandPool — vkDestroyCommandPool implicitly frees
        // all VkCommandBuffers allocated from it.  The Array::clear() that follows only
        // zeroes the pointer list; it never invokes C++ destructors, so skipping this
        // step leaks every VkCommandPool and VkCommandBuffer past vkDestroyDevice.
        for (uint32_t i = 0; i < InstantGraphicsPools.size(); ++i)
            InstantGraphicsPools[i]->~CommandPool();
        for (uint32_t i = 0; i < CommandPools.size(); ++i)
            CommandPools[i]->~CommandPool();
        for (uint32_t i = 0; i < TransferCommandPools.size(); ++i)
            TransferCommandPools[i]->~CommandPool();
        for (uint32_t i = 0; i < InstantTransferPools.size(); ++i)
            InstantTransferPools[i]->~CommandPool();

        InstantGraphicsPools.clear();
        InstantGraphicsCommandBuffers.clear();
        CommandBuffers.clear();
        TransferCommandBuffers.clear();
        InstantTransferCommandBuffers.clear();
        CommandPools.clear();
        TransferCommandPools.clear();
        InstantTransferPools.clear();
        EnqueuedCommandBuffers.clear();
    }

    CommandBuffer* CommandBufferManager::GetCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t buffer_per_pool_index, bool begin)
    {
        auto           buffer_index = ((frame_index * TotalThreadCount) + thread_index) * MaxBufferPerPool + buffer_per_pool_index;
        CommandBuffer* buffer       = (type == Rendering::QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandBuffers[buffer_index] : CommandBuffers[buffer_index];

        if (begin)
        {
            buffer->ResetState();
            buffer->Begin();
        }
        return buffer;
    }

    CommandBuffer* CommandBufferManager::GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint32_t buffer_per_pool_index, bool begin)
    {
        // MaxBufferPerPool * MaxBufferPerPool is the total number of instant command buffers per pool
        auto           buffer_index = ((frame_index * TotalThreadCount) + thread_index) * (MaxBufferPerPool * MaxBufferPerPool) + buffer_per_pool_index;
        CommandBuffer* buffer       = (type == Rendering::QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? InstantTransferCommandBuffers[buffer_index] : InstantGraphicsCommandBuffers[buffer_index];

        if (begin)
        {
            buffer->ResetState();
            vkResetCommandBuffer(buffer->GetHandle(), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            buffer->Begin();
        }
        return buffer;
    }

    Rendering::Pools::CommandPool* CommandBufferManager::GetCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        return (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? TransferCommandPools[pool_index] : CommandPools[pool_index];
    }

    Rendering::Pools::CommandPool* CommandBufferManager::GetInstantCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        return (type == QueueType::TRANSFER_QUEUE && Device->HasSeperateTransfertQueueFamily) ? InstantTransferPools[pool_index] : InstantGraphicsPools[pool_index];
    }

    void CommandBufferManager::ResetPool(uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        vkResetCommandPool(Device->LogicalDevice, CommandPools[pool_index]->Handle, 0);
        if (Device->HasSeperateTransfertQueueFamily)
        {
            vkResetCommandPool(Device->LogicalDevice, TransferCommandPools[pool_index]->Handle, 0);
        }
    }

    void CommandBufferManager::ResetEnqueuedBufferIndex()
    {
        for (int i = 0; i < EnqueuedCommandBufferIndex && i < EnqueuedCommandBuffers.size(); ++i)
        {
            if (EnqueuedCommandBuffers[i])
            {
                EnqueuedCommandBuffers[i]->SetState(CommandBufferState::Pending);
            }
        }
        EnqueuedCommandBufferIndex = 0u;
    }

    void CommandBufferManager::EndEnqueuedBuffers()
    {
        for (int i = 0; i < EnqueuedCommandBufferIndex; ++i)
        {
            EnqueuedCommandBuffers[i]->End();
        }
    }

    void CommandBufferManager::EnqueueBuffer(CommandBufferPtr const buffer)
    {
        if (EnqueuedCommandBufferIndex < EnqueuedCommandBuffers.size())
        {
            EnqueuedCommandBuffers[EnqueuedCommandBufferIndex++] = buffer;
            return;
        }
        ZENGINE_CORE_ERROR("[!] Enqueued Command Buffer overflow detected")
    }

    void CommandBufferManager::IncreaseBuffers()
    {
        // TotalPoolCount          = Device->SwapchainImageCount * TotalThreadCount;
        // TotalCommandBufferCount = TotalPoolCount * MaxBufferPerPool;

        // if (TotalCommandBufferCount > EnqueuedCommandBuffers.size())
        // {
        //     auto size = EnqueuedCommandBuffers.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         EnqueuedCommandBuffers.push(nullptr);
        //     }
        // }

        // if (TotalPoolCount > CommandPools.size())
        // {
        //     auto size = CommandPools.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         CommandPools.push(ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::GRAPHIC_QUEUE));
        //     }
        // }

        // if (TotalCommandBufferCount > CommandBuffers.size())
        // {
        //     auto size = CommandBuffers.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         int   pool_index = GetPoolFromIndex(Rendering::QueueType::GRAPHIC_QUEUE, i);
        //         auto& pool       = CommandPools[pool_index];
        //         CommandBuffers.push(ZPushStructCtorArgs(
        //             Device->Arena,
        //             CommandBuffer,
        //             Device,
        //             pool->Handle,
        //             pool->QueueType,
        //             /*(i % MaxBufferPerPool) == 0 ? false : true */ false));
        //     }
        // }

        // if (Device->HasSeperateTransfertQueueFamily)
        // {
        //     auto size = TransferCommandPools.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         TransferCommandPools.push(ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::TRANSFER_QUEUE));
        //     }

        //     size = TransferCommandBuffers.size();
        //     for (uint32_t i = size; i < TotalCommandBufferCount; ++i)
        //     {
        //         int   pool_index = GetPoolFromIndex(Rendering::QueueType::TRANSFER_QUEUE, i);
        //         auto& pool       = TransferCommandPools[pool_index];
        //         TransferCommandBuffers.push(ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, pool->Handle, pool->QueueType, true));
        //     }
        // }
    }

    bool CommandBufferManager::IsInitialized() const
    {
        return m_is_initialized;
    }
} // namespace ZEngine::Hardwares
