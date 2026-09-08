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

    struct CommandBufferManager
    {
        void                                                            Initialize(VulkanDevice* device, uint32_t image_count = 0, uint8_t override_thread_count = 0);
        void                                                            Deinitialize();
        CommandBuffer*                                                  GetCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint8_t buffer_per_pool_index, bool begin = true);
        CommandBuffer*                                                  GetInstantCommandBuffer(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index, uint32_t buffer_per_pool_index, bool begin = true);
        Rendering::Pools::CommandPool*                                  GetCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index);
        Rendering::Pools::CommandPool*                                  GetInstantCommandPool(Rendering::QueueType type, uint8_t frame_index, uint8_t thread_index);
        void                                                            ResetPool(uint8_t frame_index, uint8_t thread_index);
        void                                                            IncreaseBuffers();
        void                                                            EnqueueBuffer(CommandBufferPtr const buffer);
        void                                                            EndEnqueuedBuffers();
        void                                                            ResetEnqueuedBufferIndex();
        bool                                                            IsInitialized() const;

        uint32_t                                                        TotalCommandBufferCount        = 0;
        uint32_t                                                        TotalInstantCommandBufferCount = 0;
        uint32_t                                                        TotalPoolCount                 = 0;
        uint32_t                                                        TotalThreadCount               = 0;
        uint32_t                                                        EnqueuedCommandBufferIndex     = 0;
        const uint32_t                                                  MaxBufferPerPool               = 4;
        VulkanDevice*                                                   Device                         = nullptr;

        Core::Containers::Array<Rendering::Pools::CommandPool*>         InstantGraphicsPools           = {};
        Core::Containers::Array<Rendering::Pools::CommandPool*>         InstantTransferPools           = {};
        Core::Containers::Array<CommandBuffer*>                         InstantGraphicsCommandBuffers  = {};
        Core::Containers::Array<CommandBuffer*>                         InstantTransferCommandBuffers  = {};

        Core::Containers::Array<ZRawPtr(Rendering::Pools::CommandPool)> CommandPools                   = {};
        Core::Containers::Array<ZRawPtr(Rendering::Pools::CommandPool)> TransferCommandPools           = {};
        Core::Containers::Array<ZRawPtr(CommandBuffer)>                 CommandBuffers                 = {};
        Core::Containers::Array<ZRawPtr(CommandBuffer)>                 TransferCommandBuffers         = {};
        Core::Containers::Array<CommandBuffer*>                         EnqueuedCommandBuffers         = {};

    private:
        bool m_is_initialized = false;
    };
    ZDEFINE_PTR(CommandBufferManager);
} // namespace ZEngine::Hardwares
