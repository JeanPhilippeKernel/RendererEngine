#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Renderers
{
    struct SkyboxPass : public IRenderGraphCallbackPass
    {
        // Set before Setup() is called. Empty string = no env map, pass is disabled.
        cstring      EnvMapPath = nullptr;

        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        Textures::TextureHandle m_env_map    = {};
        uint32_t                m_vtx_offset = 0;
        uint32_t                m_idx_offset = 0;
    };
} // namespace ZEngine::Rendering::Renderers
