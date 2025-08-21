#pragma once
#include <Core/Containers/Array.h>
#include <RenderGraph.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <ZEngineDef.h>

#define WRITE_BUFFERS_ONCE(frame_index, body)          \
    if (!m_write_once_control.contains(frame_index))   \
    {                                                  \
        body m_write_once_control[frame_index] = true; \
    }

namespace ZEngine::Rendering::Renderers
{

    struct InitialPass : public IRenderGraphCallbackPass
    {
        Core::Containers::Array<float>   VertexData = {};
        Hardwares::VertexBufferSetHandle VBHandle   = {};

        virtual void                     Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void                     Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void                     Execute(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct DepthPrePass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct SkyboxPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        Hardwares::VertexBufferSetHandle  m_vb_handle   = {};
        Hardwares::IndexBufferSetHandle   m_ib_handle   = {};
        Textures::TextureHandle           m_env_map     = {};
        Core::Containers::Array<uint16_t> m_index_data  = {};
        Core::Containers::Array<float>    m_vertex_data = {};
    };

    struct GridPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        Core::Containers::Array<uint16_t> m_index_data  = {};
        Core::Containers::Array<float>    m_vertex_data = {};
        Hardwares::VertexBufferSetHandle  m_vb_handle   = {};
        Hardwares::IndexBufferSetHandle   m_ib_handle   = {};
    };

    struct GbufferPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    struct LightingPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        virtual void Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        virtual void Execute(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
    };

} // namespace ZEngine::Rendering::Renderers