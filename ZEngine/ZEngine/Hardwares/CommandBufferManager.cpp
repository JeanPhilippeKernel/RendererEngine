#include <ZEngine/Hardwares/CommandBufferManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Logging/LoggerDefinition.h>

using namespace ZEngine::Rendering;

namespace ZEngine::Hardwares
{
    static WorkerSecondaryPool& ResolveWorkerSecondaryPool(CommandBufferManager* manager, Rendering::QueueType type, uint32_t pool_index)
    {
        if (type == QueueType::TRANSFER_QUEUE && manager->Device->HasSeparateTransferQueue)
            return manager->WorkerSecondaryTransfers[pool_index];
        if (type == QueueType::COMPUTE_QUEUE && manager->Device->HasSeparateComputeQueue)
            return manager->WorkerSecondaryComputes[pool_index];
        return manager->WorkerSecondaryGraphics[pool_index];
    }

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
        TotalGraphCommandBufferCount   = TotalPoolCount * MaxGraphBatchesPerPool;

        // Graph primary/secondary buffers are created lazily from inside
        // RenderGraph::Execute(). That function uses Device->Arena as scratch
        // storage, so give persistent command-buffer state an independent arena.
        // 256 MiB supports more than 2,000 120 KiB recording arenas while only
        // reserving virtual address space until the buffers are actually used.
        Device->Arena->CreateSubArena(ZMega(256), &m_render_graph_command_buffer_arena);

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

        // Graph batches always need primary buffers and must not collide with
        // the normal primary/secondary or instant-upload allocations. The slots
        // are allocated lazily by GetGraphBatchCommandBuffer(): reserving a
        // reasonably-sized batch budget must not also reserve each buffer's
        // 120 KiB recording arena on devices that never use async queues.
        GraphGraphicsCommandBuffers.init(Device->Arena, TotalGraphCommandBufferCount, TotalGraphCommandBufferCount);
        for (uint32_t i = 0; i < TotalPoolCount; ++i)
            for (uint32_t buffer_index = 0; buffer_index < MaxGraphBatchesPerPool; ++buffer_index)
                GraphGraphicsCommandBuffers[i * MaxGraphBatchesPerPool + buffer_index] = nullptr;

        WorkerSecondaryGraphics.init(Device->Arena, TotalPoolCount, TotalPoolCount);
        for (uint32_t i = 0; i < TotalPoolCount; ++i)
        {
            auto& worker_pool = WorkerSecondaryGraphics[i];
            worker_pool.Pool  = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, QueueType::GRAPHIC_QUEUE);
            worker_pool.Buffers.init(&m_render_graph_command_buffer_arena, 4);
        }

        if (Device->HasSeparateTransferQueue)
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
            GraphTransferCommandBuffers.init(Device->Arena, TotalGraphCommandBufferCount, TotalGraphCommandBufferCount);
            for (uint32_t i = 0; i < TotalPoolCount; ++i)
                for (uint32_t buffer_index = 0; buffer_index < MaxGraphBatchesPerPool; ++buffer_index)
                    GraphTransferCommandBuffers[i * MaxGraphBatchesPerPool + buffer_index] = nullptr;

            WorkerSecondaryTransfers.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            for (uint32_t i = 0; i < TotalPoolCount; ++i)
            {
                auto& worker_pool = WorkerSecondaryTransfers[i];
                worker_pool.Pool  = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, QueueType::TRANSFER_QUEUE);
                worker_pool.Buffers.init(&m_render_graph_command_buffer_arena, 4);
            }
        }

        if (Device->HasSeparateComputeQueue)
        {
            InstantComputePools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            ComputeCommandPools.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            ComputeCommandBuffers.init(Device->Arena, TotalCommandBufferCount, TotalCommandBufferCount);
            InstantComputeCommandBuffers.init(Device->Arena, TotalInstantCommandBufferCount, TotalInstantCommandBufferCount);

            for (uint32_t i = 0; i < TotalPoolCount; ++i)
            {
                InstantComputePools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::COMPUTE_QUEUE);
                for (uint32_t buf_idx = 0; buf_idx < (MaxBufferPerPool * MaxBufferPerPool); ++buf_idx)
                {
                    uint32_t buffer_idx                      = (i * (MaxBufferPerPool * MaxBufferPerPool)) + buf_idx;
                    InstantComputeCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, InstantComputePools[i]->Handle, InstantComputePools[i]->QueueType, true);
                }

                ComputeCommandPools[i] = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, Rendering::QueueType::COMPUTE_QUEUE);
                for (uint32_t buf_idx = 0; buf_idx < MaxBufferPerPool; ++buf_idx)
                {
                    uint32_t buffer_idx               = (i * MaxBufferPerPool) + buf_idx;
                    ComputeCommandBuffers[buffer_idx] = ZPushStructCtorArgs(Device->Arena, CommandBuffer, Device, ComputeCommandPools[i]->Handle, ComputeCommandPools[i]->QueueType, true);
                }
            }
            GraphComputeCommandBuffers.init(Device->Arena, TotalGraphCommandBufferCount, TotalGraphCommandBufferCount);
            for (uint32_t i = 0; i < TotalPoolCount; ++i)
                for (uint32_t buffer_index = 0; buffer_index < MaxGraphBatchesPerPool; ++buffer_index)
                    GraphComputeCommandBuffers[i * MaxGraphBatchesPerPool + buffer_index] = nullptr;

            WorkerSecondaryComputes.init(Device->Arena, TotalPoolCount, TotalPoolCount);
            for (uint32_t i = 0; i < TotalPoolCount; ++i)
            {
                auto& worker_pool = WorkerSecondaryComputes[i];
                worker_pool.Pool  = ZPushStructCtorArgs(Device->Arena, Rendering::Pools::CommandPool, Device, QueueType::COMPUTE_QUEUE);
                worker_pool.Buffers.init(&m_render_graph_command_buffer_arena, 4);
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
        for (uint32_t i = 0; i < ComputeCommandPools.size(); ++i)
            ComputeCommandPools[i]->~CommandPool();
        for (uint32_t i = 0; i < InstantComputePools.size(); ++i)
            InstantComputePools[i]->~CommandPool();
        for (uint32_t i = 0; i < WorkerSecondaryGraphics.size(); ++i)
            WorkerSecondaryGraphics[i].Pool->~CommandPool();
        for (uint32_t i = 0; i < WorkerSecondaryTransfers.size(); ++i)
            WorkerSecondaryTransfers[i].Pool->~CommandPool();
        for (uint32_t i = 0; i < WorkerSecondaryComputes.size(); ++i)
            WorkerSecondaryComputes[i].Pool->~CommandPool();

        // The graph pools release their VkCommandBuffers. Their recording arenas
        // are independent sub-arenas and must be released before their parent.
        for (CommandBuffer* buffer : GraphGraphicsCommandBuffers)
            if (buffer)
                buffer->LocalArena.Shutdown();
        for (CommandBuffer* buffer : GraphTransferCommandBuffers)
            if (buffer)
                buffer->LocalArena.Shutdown();
        for (CommandBuffer* buffer : GraphComputeCommandBuffers)
            if (buffer)
                buffer->LocalArena.Shutdown();
        for (const WorkerSecondaryPool& pool : WorkerSecondaryGraphics)
            for (CommandBuffer* buffer : pool.Buffers)
                if (buffer)
                    buffer->LocalArena.Shutdown();
        for (const WorkerSecondaryPool& pool : WorkerSecondaryTransfers)
            for (CommandBuffer* buffer : pool.Buffers)
                if (buffer)
                    buffer->LocalArena.Shutdown();
        for (const WorkerSecondaryPool& pool : WorkerSecondaryComputes)
            for (CommandBuffer* buffer : pool.Buffers)
                if (buffer)
                    buffer->LocalArena.Shutdown();

        InstantGraphicsPools.clear();
        InstantGraphicsCommandBuffers.clear();
        CommandBuffers.clear();
        TransferCommandBuffers.clear();
        ComputeCommandBuffers.clear();
        GraphGraphicsCommandBuffers.clear();
        GraphTransferCommandBuffers.clear();
        GraphComputeCommandBuffers.clear();
        WorkerSecondaryGraphics.clear();
        WorkerSecondaryTransfers.clear();
        WorkerSecondaryComputes.clear();
        InstantTransferCommandBuffers.clear();
        InstantComputeCommandBuffers.clear();
        CommandPools.clear();
        TransferCommandPools.clear();
        ComputeCommandPools.clear();
        InstantTransferPools.clear();
        InstantComputePools.clear();
        EnqueuedCommandBuffers.clear();
        m_render_graph_command_buffer_arena.Shutdown();
    }

    CommandBuffer* CommandBufferManager::GetCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t buffer_per_pool_index, bool begin)
    {
        auto           buffer_index = ((frame_index * TotalThreadCount) + thread_index) * MaxBufferPerPool + buffer_per_pool_index;
        CommandBuffer* buffer       = type == Rendering::QueueType::TRANSFER_QUEUE && Device->HasSeparateTransferQueue ? TransferCommandBuffers[buffer_index] : type == Rendering::QueueType::COMPUTE_QUEUE && Device->HasSeparateComputeQueue ? ComputeCommandBuffers[buffer_index] : CommandBuffers[buffer_index];

        if (begin)
        {
            buffer->ResetState();
            buffer->Begin();
        }
        return buffer;
    }

    CommandBuffer* CommandBufferManager::GetGraphBatchCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t batch_index, bool begin)
    {
        ZENGINE_VALIDATE_ASSERT(static_cast<uint32_t>(type) < static_cast<uint32_t>(QueueType::COUNT), "Invalid render graph queue type")
        ZENGINE_VALIDATE_ASSERT(batch_index < MaxGraphBatchesPerPool, "Render graph batch command buffer overflow")
        const uint32_t  index         = ((frame_index * TotalThreadCount) + thread_index) * MaxGraphBatchesPerPool + batch_index;
        const uint32_t  pool_index    = (frame_index * TotalThreadCount) + thread_index;
        CommandBuffer** slot          = &GraphGraphicsCommandBuffers[index];
        VkCommandPool   command_pool  = CommandPools[pool_index]->Handle;
        QueueType       resolved_type = QueueType::GRAPHIC_QUEUE;
        if (type == QueueType::TRANSFER_QUEUE && Device->HasSeparateTransferQueue)
        {
            slot          = &GraphTransferCommandBuffers[index];
            command_pool  = TransferCommandPools[pool_index]->Handle;
            resolved_type = QueueType::TRANSFER_QUEUE;
        }
        else if (type == QueueType::COMPUTE_QUEUE && Device->HasSeparateComputeQueue)
        {
            slot          = &GraphComputeCommandBuffers[index];
            command_pool  = ComputeCommandPools[pool_index]->Handle;
            resolved_type = QueueType::COMPUTE_QUEUE;
        }

        if (!*slot)
            *slot = ZPushStructCtorArgs(&m_render_graph_command_buffer_arena, CommandBuffer, Device, command_pool, resolved_type, true, &m_render_graph_command_buffer_arena);
        CommandBuffer* buffer = *slot;
        if (begin)
        {
            buffer->ResetState();
            vkResetCommandBuffer(buffer->GetHandle(), 0);
            buffer->Begin();
        }
        return buffer;
    }

    void CommandBufferManager::BeginWorkerSecondaryFrame(uint8_t frame_index)
    {
        ZENGINE_VALIDATE_ASSERT(frame_index < Device->SwapchainPtr->BufferredFrameCount, "Invalid worker-secondary frame index")
        for (uint32_t worker = 0; worker < TotalThreadCount; ++worker)
        {
            const uint32_t index = frame_index * TotalThreadCount + worker;
            vkResetCommandPool(Device->LogicalDevice, WorkerSecondaryGraphics[index].Pool->Handle, 0);
            if (Device->HasSeparateTransferQueue)
                vkResetCommandPool(Device->LogicalDevice, WorkerSecondaryTransfers[index].Pool->Handle, 0);
            if (Device->HasSeparateComputeQueue)
                vkResetCommandPool(Device->LogicalDevice, WorkerSecondaryComputes[index].Pool->Handle, 0);
        }
    }

    void CommandBufferManager::PrepareWorkerSecondary(Rendering::QueueType type, uint8_t frame_index, uint8_t worker_index, uint32_t count)
    {
        ZENGINE_VALIDATE_ASSERT(frame_index < Device->SwapchainPtr->BufferredFrameCount, "Invalid worker-secondary frame index")
        ZENGINE_VALIDATE_ASSERT(worker_index < TotalThreadCount, "Invalid worker-secondary worker index")

        WorkerSecondaryPool& pool = ResolveWorkerSecondaryPool(this, type, frame_index * TotalThreadCount + worker_index);
        while (pool.Buffers.size() < count)
        {
            CommandBuffer* buffer = ZPushStructCtorArgs(&m_render_graph_command_buffer_arena, CommandBuffer, Device, pool.Pool->Handle, pool.Pool->QueueType, false, &m_render_graph_command_buffer_arena);
            pool.Buffers.push(buffer);
        }
    }

    CommandBuffer* CommandBufferManager::AcquireWorkerSecondary(Rendering::QueueType type, uint8_t frame_index, uint8_t worker_index, uint32_t ordinal)
    {
        ZENGINE_VALIDATE_ASSERT(frame_index < Device->SwapchainPtr->BufferredFrameCount, "Invalid worker-secondary frame index")
        ZENGINE_VALIDATE_ASSERT(worker_index < TotalThreadCount, "Invalid worker-secondary worker index")

        WorkerSecondaryPool& pool = ResolveWorkerSecondaryPool(this, type, frame_index * TotalThreadCount + worker_index);
        ZENGINE_VALIDATE_ASSERT(ordinal < pool.Buffers.size(), "Worker secondary buffer was not prepared")
        CommandBuffer* buffer = pool.Buffers[ordinal];
        ZENGINE_VALIDATE_ASSERT(buffer != nullptr && buffer->BufferType == CommandBufferType::Secondary, "Worker secondary buffer is invalid")
        buffer->ResetState();
        return buffer;
    }

    CommandBuffer* CommandBufferManager::GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint32_t buffer_per_pool_index, bool begin)
    {
        // MaxBufferPerPool * MaxBufferPerPool is the total number of instant command buffers per pool
        auto           buffer_index = ((frame_index * TotalThreadCount) + thread_index) * (MaxBufferPerPool * MaxBufferPerPool) + buffer_per_pool_index;
        CommandBuffer* buffer       = type == Rendering::QueueType::TRANSFER_QUEUE && Device->HasSeparateTransferQueue ? InstantTransferCommandBuffers[buffer_index] : type == Rendering::QueueType::COMPUTE_QUEUE && Device->HasSeparateComputeQueue ? InstantComputeCommandBuffers[buffer_index] : InstantGraphicsCommandBuffers[buffer_index];

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
        return type == QueueType::TRANSFER_QUEUE && Device->HasSeparateTransferQueue ? TransferCommandPools[pool_index] : type == QueueType::COMPUTE_QUEUE && Device->HasSeparateComputeQueue ? ComputeCommandPools[pool_index] : CommandPools[pool_index];
    }

    Rendering::Pools::CommandPool* CommandBufferManager::GetInstantCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        return type == QueueType::TRANSFER_QUEUE && Device->HasSeparateTransferQueue ? InstantTransferPools[pool_index] : type == QueueType::COMPUTE_QUEUE && Device->HasSeparateComputeQueue ? InstantComputePools[pool_index] : InstantGraphicsPools[pool_index];
    }

    void CommandBufferManager::ResetPool(uint8_t frame_index, uint8_t thread_index)
    {
        uint32_t pool_index = (frame_index * TotalThreadCount) + thread_index;
        vkResetCommandPool(Device->LogicalDevice, CommandPools[pool_index]->Handle, 0);
        if (Device->HasSeparateTransferQueue)
        {
            vkResetCommandPool(Device->LogicalDevice, TransferCommandPools[pool_index]->Handle, 0);
        }
        if (Device->HasSeparateComputeQueue)
            vkResetCommandPool(Device->LogicalDevice, ComputeCommandPools[pool_index]->Handle, 0);
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
