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
        const char*                                                      DebugName               = {};
        bool                                                             SwapchainAsRenderTarget = false;
        Specifications::GraphicRendererPipelineSpecification             PipelineSpecification   = {};
        std::vector<Helpers::Ref<Textures::Texture>>                     Inputs                  = {};
        std::unordered_map<std::string, Helpers::Ref<Textures::Texture>> InputTextures           = {};
        std::vector<Specifications::TextureSpecification>                Outputs                 = {};
        std::vector<Helpers::Ref<Textures::Texture>>                     ExternalOutputs         = {};
    };
} // namespace ZEngine::Rendering::Specifications
