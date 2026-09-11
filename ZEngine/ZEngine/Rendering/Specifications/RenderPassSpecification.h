#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Rendering/Specifications/GraphicRendererPipelineSpecification.h>
#include <ZEngine/Rendering/Specifications/TextureSpecification.h>
#include <ZEngine/Rendering/Textures/Texture.h>

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
        const char*                                                              ComputeShaderName       = nullptr;
        uint32_t                                                                 ComputePushConstantSize = 0;
        Core::Containers::Array<Textures::TextureHandle>                         Inputs                  = {};
        Core::Containers::UnorderedHashMap<const char*, Textures::TextureHandle> InputTextures           = {};
        Core::Containers::Array<Specifications::TextureSpecification>            Outputs                 = {};
        Core::Containers::Array<Textures::TextureHandle>                         ExternalOutputs         = {};
        // One entry per ExternalOutputs item. Load behaviour belongs to a pass use,
        // rather than to the backing texture: the same image may be cleared by its
        // first writer and loaded by later overlay passes.
        Core::Containers::Array<LoadOperation>                                   ExternalOutputLoadOps   = {};
    };
} // namespace ZEngine::Rendering::Specifications
