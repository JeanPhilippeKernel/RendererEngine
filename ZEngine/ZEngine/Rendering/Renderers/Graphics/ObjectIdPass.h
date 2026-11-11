#pragma once
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>

namespace ZEngine::Rendering::Renderers
{
    // Picking needs its own depth: blended color never writes scene depth, but
    // the closest nonzero-alpha surface must own the integer selection token.
    struct ObjectIdPass final : GbufferPass
    {
        bool Register(Hardwares::VulkanDevicePtr device, cstring name, const RenderGraphFrameContext& frame, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr inspector) override;
        bool TryBuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena, Specifications::GraphicsPipelineDesc* out) const override;
        bool RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr commands) override;
    };
} // namespace ZEngine::Rendering::Renderers
