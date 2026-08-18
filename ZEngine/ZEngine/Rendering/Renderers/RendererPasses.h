#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Renderers/RenderPasses/RenderPass.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Rendering::Renderers
{

    struct BasePass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct DepthPrePass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

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
        GridPushConstantData PushData = {};

        virtual void         Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void         Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void         Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        uint32_t m_vtx_offset = 0;
        uint32_t m_idx_offset = 0;
    };

    struct GbufferPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct LightingPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

} // namespace ZEngine::Rendering::Renderers