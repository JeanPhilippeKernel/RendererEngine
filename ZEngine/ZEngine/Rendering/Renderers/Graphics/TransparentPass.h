#pragma once
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>

namespace ZEngine::Rendering::Renderers
{
    // Conventional back-to-front transparency over the lit HDR scene. Inherits
    // only the environment state and render-pass execution plumbing, not the
    // deferred fullscreen draw or its attachment declarations.
    struct TransparentPass final : LightingPass
    {
        void SetUseCompositedSceneColor(bool value)
        {
            m_use_composited = value;
        }
        bool Register(Hardwares::VulkanDevicePtr, cstring, const RenderGraphFrameContext&, RenderGraphResourceBuilderPtr, RenderGraphResourceInspectorPtr) override;
        bool TryBuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena, Specifications::GraphicsPipelineDesc* out) const override;
        void Prepare(Hardwares::VulkanDevicePtr, Scenes::SceneDataPtr, RenderGraphResourceInspectorPtr, RenderPasses::RenderPass*) override;
        bool RecordDraw(Hardwares::VulkanDevicePtr, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr, RenderPasses::RenderPass*, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr) override;

    private:
        bool m_use_composited = false;
    };
} // namespace ZEngine::Rendering::Renderers
