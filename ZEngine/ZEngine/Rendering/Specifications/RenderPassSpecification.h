#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Specifications
{
    enum class RenderPassType
    {
        GRAPHIC,
        COMPUTE,
        TRANSFER
    };

    struct RenderPassSpecification
    {
        const char*                                                              DebugName               = {};
        bool                                                                     SwapchainAsRenderTarget = false;
        RenderPassType                                                           Type                    = {RenderPassType::GRAPHIC};
        Specifications::GraphicRendererPipelineSpecification                     PipelineSpecification   = {};
        Core::Containers::Array<Textures::TextureHandle>                         Inputs                  = {};
        Core::Containers::UnorderedHashMap<const char*, Textures::TextureHandle> InputTextures           = {};
        Core::Containers::Array<Specifications::TextureSpecification>            Outputs                 = {};
        Core::Containers::Array<Textures::TextureHandle>                         ExternalOutputs         = {};
    };
} // namespace ZEngine::Rendering::Specifications
