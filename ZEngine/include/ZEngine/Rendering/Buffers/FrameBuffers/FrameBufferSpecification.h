#pragma once
#include <Rendering/Buffers/FrameBuffers/FrameBufferAttachmentSpecification.h>

namespace ZEngine::Rendering::Buffers
{
    struct FrameBufferSpecificationVNext
    {
        uint32_t                                              Height;
        uint32_t                                              Width;
        uint32_t                                              Layers = 1;
        FrameBuffers::FrameBufferAttachmentSpecificationVNext AttachmentSpecifications;
    };

} // namespace ZEngine::Rendering::Buffers
