#pragma once

namespace ZEngine::Rendering
{
    enum class QueueType
    {
        GRAPHIC_QUEUE = 0,
        TRANSFER_QUEUE
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
        SEMAPHORE,
        FENCE,
        DESCRIPTORSETLAYOUT,
        DESCRIPTORPOOL,
        DESCRIPTORSET,
        RESOURCE_COUNT
    };
}