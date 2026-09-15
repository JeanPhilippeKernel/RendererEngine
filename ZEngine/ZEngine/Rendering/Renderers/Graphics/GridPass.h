#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::Rendering::Renderers
{
    struct GridPushConstantData
    {
        float ColorThin[4]  = {0.6f, 0.6f, 0.6f, 1.0f};
        float ColorThick[4] = {0.3f, 0.3f, 0.3f, 1.0f};
        float ColorXAxis[4] = {0.9f, 0.2f, 0.2f, 1.0f};
        float ColorZAxis[4] = {0.2f, 0.4f, 1.0f, 1.0f};
        float CellSize      = 0.025f;
        float FadeStrength  = 0.5f;
        float FadeRadius    = 500.0f;
        float LineWidth     = 1.5f;
        int   MaxLOD        = 5;
        float GroundY       = 0.0f;
        float _pad[2]       = {};
    };

    struct GridPass : public IRenderGraphCallbackPass
    {
        GridPushConstantData                 PushData = {};
        bool                                 Enabled  = true;

        /// @brief Builds the static PSO recipe used by the infinite-grid effect.
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;

        bool                                 Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        void                                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        void                                 Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }

    private:
        uint32_t m_vtx_offset          = 0;
        uint32_t m_idx_offset          = 0;
        bool     m_geometry_registered = false;
    };
} // namespace ZEngine::Rendering::Renderers
