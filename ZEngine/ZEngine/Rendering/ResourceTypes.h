#pragma once

namespace ZEngine::Rendering
{
    enum class QueueType
    {
        GRAPHIC_QUEUE = 0,
        TRANSFER_QUEUE,
        COMPUTE_QUEUE,
        // Sentinel for fixed queue-indexed storage; never a Vulkan queue.
        COUNT,
    };

    enum class DeviceResourceType
    {
        SAMPLER,
        FRAMEBUFFER,
        IMAGEVIEW,
        IMAGE,
        RENDERPASS,
        BUFFER,
        BUFFERMEMORY,
        PIPELINE_LAYOUT,
        PIPELINE,
        SHADERMODULE,
        SEMAPHORE,
        FENCE,
        DESCRIPTORSETLAYOUT,
        DESCRIPTORPOOL,
        DESCRIPTORSET,
        QUERYPOOL,
        RESOURCE_COUNT
    };
} // namespace ZEngine::Rendering
