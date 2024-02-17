#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Specifications
{
    struct RenderPassSpecification
    {
        const char*                                          DebugName               = {};
        bool                                                 SwapchainAsRenderTarget = false;
        Specifications::GraphicRendererPipelineSpecification PipelineSpecification   = {};
        std::vector<Ref<Textures::Texture>>                  Inputs                  = {};
        std::vector<Specifications::TextureSpecification>    Outputs                 = {};
        std::vector<Ref<Textures::Texture>>                  ExternalOutputs         = {};
    };
} // namespace ZEngine::Rendering::Specifications
