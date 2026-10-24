#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/GridLabelData.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    inline constexpr cstring GridLabelAtlasName = "GridLabelAtlas";

    /// At most two atlas quads, after Grid and before the always-visible Gizmo.
    /// No vertex/index buffer, camera uniform, sampler or per-frame allocation.
    struct GridLabelPass final : IRenderGraphCallbackPass
    {
        void                                 SetFrame(const GridLabelFrameData& frame);
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        bool                                 Register(Hardwares::VulkanDevicePtr device, cstring name, const RenderGraphFrameContext& context, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr inspector) override;
        void                                 Prepare(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr scene, RenderGraphResourceInspectorPtr inspector, RenderPasses::RenderPass* pass) override;
        void                                 Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }

    private:
        GridLabelFrameData m_frame = {};
        RGResourceHandle   m_atlas = {};
        RGResourceHandle   m_depth = {};
    };
} // namespace ZEngine::Rendering::Renderers
