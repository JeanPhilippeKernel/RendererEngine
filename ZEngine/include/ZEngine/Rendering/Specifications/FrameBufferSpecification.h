#pragma once
#include <map>
#include <Rendering/Buffers/Image2DBuffer.h>
#include <Rendering/Specifications/FrameBufferAttachmentSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct FrameBufferSpecificationVNext
    {
        bool                                            ClearColor         = false;
        bool                                            ClearDepth         = false;
        float                                           ClearColorValue[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        float                                           ClearDepthValue[2] = {1.0f, 0.0f};
        uint32_t                                        Width              = 1;
        uint32_t                                        Height             = 1;
        uint32_t                                        Layers             = 1;
        std::map<uint32_t, Ref<Buffers::Image2DBuffer>> InputColorAttachment;
        FrameBufferAttachmentSpecificationVNext         AttachmentSpecifications;
    };

} // namespace ZEngine::Rendering::Specifications
