#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Rendering/Pools/CommandPool.h>
#include <ZEngine/Rendering/ResourceTypes.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;

    struct CommandBuffer;
    ZDEFINE_PTR(CommandBuffer);

    /// @brief Secondary buffers exclusively recorded by one worker for one frame slot.
    struct WorkerSecondaryPool
    {
        Rendering::Pools::CommandPool*          Pool    = nullptr;
        Core::Containers::Array<CommandBuffer*> Buffers = {};
    };

    struct CommandBufferManager
    {
        /// @brief Creates command pools and buffers for the selected device.
        void                                                    Initialize(VulkanDevice* device, uint32_t image_count = 0, uint8_t override_thread_count = 0);
        /// @brief Releases all manager-owned command pools and buffers.
        void                                                    Deinitialize();
        /// @brief Returns a regular command buffer for a frame, thread, and queue role.
        CommandBuffer*                                          GetCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t buffer_per_pool_index, bool begin = true);
        /// @brief Returns a primary command buffer reserved for one render-graph queue batch.
        CommandBuffer*                                          GetGraphBatchCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t batch_index, bool begin = true);
        /// @brief Resets all dedicated worker-secondary pools for a completed frame slot.
        void                                                    BeginWorkerSecondaryFrame(uint8_t frame_index);
        /// @brief Grows one worker's dedicated secondary-buffer pool on the render thread.
        void                                                    PrepareWorkerSecondary(Rendering::QueueType type, uint8_t frame_index, uint8_t worker_index, uint32_t count);
        /// @brief Acquires a preallocated secondary buffer owned by the given worker.
        CommandBuffer*                                          AcquireWorkerSecondary(Rendering::QueueType type, uint8_t frame_index, uint8_t worker_index, uint32_t ordinal);
        /// @brief Returns an instant-submit command buffer for the selected queue role.
        CommandBuffer*                                          GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint32_t buffer_per_pool_index, bool begin = true);
        /// @brief Returns the regular command pool for a frame, thread, and queue role.
        Rendering::Pools::CommandPool*                          GetCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index);
        /// @brief Returns the instant-submit command pool for a frame, thread, and queue role.
        Rendering::Pools::CommandPool*                          GetInstantCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index);
        void                                                    ResetPool(uint8_t frame_index, uint8_t thread_index);
        void                                                    IncreaseBuffers();
        void                                                    EnqueueBuffer(CommandBufferPtr const buffer);
        void                                                    EndEnqueuedBuffers();
        void                                                    ResetEnqueuedBufferIndex();
        /// @brief Returns whether the manager currently owns initialized command resources.
        bool                                                    IsInitialized() const;

        uint32_t                                                TotalCommandBufferCount        = 0;
        uint32_t                                                TotalInstantCommandBufferCount = 0;
        uint32_t                                                TotalGraphCommandBufferCount   = 0;
        uint32_t                                                TotalPoolCount                 = 0;
        uint32_t                                                TotalThreadCount               = 0;
        uint32_t                                                EnqueuedCommandBufferIndex     = 0;
        const uint32_t                                          MaxBufferPerPool               = 4;
        // Graph scheduling is independent from normal/secondary command-buffer
        // use. Keep a dedicated budget so an alternating graphics/compute/transfer
        // graph cannot consume the application's four regular buffers.
        const uint32_t                                          MaxGraphBatchesPerPool         = 16;
        VulkanDevice*                                           Device                         = nullptr;

        Core::Containers::Array<Rendering::Pools::CommandPool*> InstantGraphicsPools           = {};
        Core::Containers::Array<Rendering::Pools::CommandPool*> InstantTransferPools           = {};
        Core::Containers::Array<Rendering::Pools::CommandPool*> InstantComputePools            = {};
        Core::Containers::Array<CommandBuffer*>                 InstantGraphicsCommandBuffers  = {};
        Core::Containers::Array<CommandBuffer*>                 InstantTransferCommandBuffers  = {};
        Core::Containers::Array<CommandBuffer*>                 InstantComputeCommandBuffers   = {};

        Core::Containers::Array<Rendering::Pools::CommandPool*> CommandPools                   = {};
        Core::Containers::Array<Rendering::Pools::CommandPool*> TransferCommandPools           = {};
        Core::Containers::Array<Rendering::Pools::CommandPool*> ComputeCommandPools            = {};
        Core::Containers::Array<CommandBuffer*>                 CommandBuffers                 = {};
        Core::Containers::Array<CommandBuffer*>                 TransferCommandBuffers         = {};
        Core::Containers::Array<CommandBuffer*>                 ComputeCommandBuffers          = {};
        Core::Containers::Array<CommandBuffer*>                 GraphGraphicsCommandBuffers    = {};
        Core::Containers::Array<CommandBuffer*>                 GraphTransferCommandBuffers    = {};
        Core::Containers::Array<CommandBuffer*>                 GraphComputeCommandBuffers     = {};
        Core::Containers::Array<WorkerSecondaryPool>            WorkerSecondaryGraphics        = {};
        Core::Containers::Array<WorkerSecondaryPool>            WorkerSecondaryTransfers       = {};
        Core::Containers::Array<WorkerSecondaryPool>            WorkerSecondaryComputes        = {};
        Core::Containers::Array<CommandBuffer*>                 EnqueuedCommandBuffers         = {};

    private:
        // Persistent storage for lazily-created render-graph command buffers.
        // RenderGraph::Execute() owns a scratch scope on Device->Arena, so graph
        // command-buffer objects, their arrays, and their recording arenas must
        // not allocate from Device->Arena directly.
        Core::Memory::ArenaAllocator m_render_graph_command_buffer_arena = {};
        bool                         m_is_initialized                    = false;
    };
    ZDEFINE_PTR(CommandBufferManager);
} // namespace ZEngine::Hardwares
