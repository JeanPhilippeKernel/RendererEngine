#pragma once
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Sky/SkyConfig.h>

namespace ZEngine::Rendering::Renderers
{
    struct SkySpherePass : public IRenderGraphCallbackPass
    {
        Sky::SkyConfig Config = {};

        void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };
} // namespace ZEngine::Rendering::Renderers
