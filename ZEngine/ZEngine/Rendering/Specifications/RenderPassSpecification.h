#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Rendering/Specifications/GraphicsPipelineDesc.h>
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
        /// @brief Diagnostic name for this backend pass.
        const char*                                      DebugName               = {};
        /// @brief Uses the current swapchain image as a color target.
        bool                                             SwapchainAsRenderTarget = false;
        /// @brief Backend pass implementation to create.
        RenderPassType                                   Type                    = {RenderPassType::GRAPHIC};
        /// @brief Static PSO state for a graphics pass.
        Specifications::GraphicsPipelineDesc             PipelineDescription     = {};
        /// @brief Compute shader asset name for a compute pass.
        const char*                                      ComputeShaderName       = nullptr;
        /// @brief Compute push-constant block size in bytes.
        uint32_t                                         ComputePushConstantSize = 0;
        /// @brief Depth textures read by this pass.
        Core::Containers::Array<Textures::TextureHandle> Inputs                  = {};
        /// @brief Non-swapchain color and depth targets written by this pass.
        Core::Containers::Array<Textures::TextureHandle> ExternalOutputs         = {};
        /// @brief Load operations corresponding one-to-one with ExternalOutputs.
        /// @details Load behavior is per pass use: one image may be cleared by its
        /// first writer and loaded by a later overlay pass.
        Core::Containers::Array<LoadOperation>           ExternalOutputLoadOps   = {};
    };
} // namespace ZEngine::Rendering::Specifications
