#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/Attachment.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Specifications
{
    struct FrameBufferSpecificationVNext
    {
        float                                 ClearColorValue[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        float                                 ClearDepthValue[2] = {1.0f, 0.0f};
        uint32_t                              Width              = 1;
        uint32_t                              Height             = 1;
        uint32_t                              Layers             = 1;
        Core::Containers::ArrayView<uint32_t> RenderTargets      = {};
        Renderers::RenderPasses::Attachment*  Attachment         = {};
    };

} // namespace ZEngine::Rendering::Specifications
