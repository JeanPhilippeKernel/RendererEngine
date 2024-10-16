#pragma once
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Rendering/Textures/Texture.h>
#include <map>

namespace ZEngine::Rendering::Specifications
{
    struct FrameBufferSpecificationVNext
    {
        float                                    ClearColorValue[4] = {0.1f, 0.1f, 0.1f, 1.0f};
        float                                    ClearDepthValue[2] = {1.0f, 0.0f};
        uint32_t                                 Width              = 1;
        uint32_t                                 Height             = 1;
        uint32_t                                 Layers             = 1;
        std::vector<VkImageView>                 RenderTargetViews  = {};
        Ref<Renderers::RenderPasses::Attachment> Attachment         = {};
    };

} // namespace ZEngine::Rendering::Specifications
