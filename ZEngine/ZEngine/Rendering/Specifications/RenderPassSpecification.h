#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <Rendering/Textures/Texture.h>
#include <unordered_map>
#include <vector>

namespace ZEngine::Rendering::Specifications
{
    struct RenderPassSpecification
    {
        const char*                                              DebugName               = {};
        bool                                                     SwapchainAsRenderTarget = false;
        Specifications::GraphicRendererPipelineSpecification     PipelineSpecification   = {};
        std::vector<Textures::TextureHandle>                     Inputs                  = {};
        std::unordered_map<std::string, Textures::TextureHandle> InputTextures           = {};
        std::vector<Specifications::TextureSpecification>        Outputs                 = {};
        std::vector<Textures::TextureHandle>                     ExternalOutputs         = {};
    };
} // namespace ZEngine::Rendering::Specifications
