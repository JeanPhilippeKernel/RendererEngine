#pragma once
#include <map>
#include <Rendering/Textures/Texture.h>
#include <Rendering/Specifications/FrameBufferAttachmentSpecification.h>

namespace ZEngine::Rendering::Specifications
{
    struct FrameBufferSpecificationVNext
    {
        bool                                       ClearColor         = false;
        bool                                       ClearDepth         = false;
        float                                      ClearColorValue[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        float                                      ClearDepthValue[2] = {1.0f, 0.0f};
        uint32_t                                   Width              = 1;
        uint32_t                                   Height             = 1;
        uint32_t                                   Layers             = 1;
        std::map<uint32_t, Ref<Textures::Texture>> InputColorAttachment;
        FrameBufferAttachmentSpecificationVNext    AttachmentSpecifications;
    };

} // namespace ZEngine::Rendering::Specifications
