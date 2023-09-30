#pragma once
#include <Rendering/Specifications/FrameBufferAttachmentSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct FrameBufferSpecificationVNext
    {
        uint32_t                                              Width;
        uint32_t                                              Height;
        uint32_t                                              Layers = 1;
        FrameBufferAttachmentSpecificationVNext AttachmentSpecifications;
    };

} // namespace ZEngine::Rendering::Specifications
