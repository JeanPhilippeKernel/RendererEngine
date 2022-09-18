#pragma once
#include <vector>
#include <Rendering/Buffers/FrameBuffers/FrameBufferAttachmentSpecification.h>

namespace ZEngine::Rendering::Buffers
{

    struct FrameBufferSpecification
    {
        uint32_t                                                           Height;
        uint32_t                                                           Width;
        uint32_t                                                           Samples;
        std::vector<FrameBuffers::FrameBufferColorAttachmentSpecification> ColorAttachmentSpecifications;
        FrameBuffers::FrameBufferDepthAttachmentSpecification              DepthAttachmentSpecification;
    };

} // namespace ZEngine::Rendering::Buffers
