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
        virtual void Setup(std::string_view name, RenderGraph* const graph) override;
        virtual void Compile(RenderPasses::RenderPass** pass, RenderGraph* const graph) override;
        virtual void Execute(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
        virtual void Render(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;

    private:
        Core::Containers::Array<float>   m_vertex_data = {};
        Hardwares::VertexBufferSetHandle m_vb_handle   = {};
    };

    struct DepthPrePass : public IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph) override;
        virtual void Compile(RenderPasses::RenderPass** pass, RenderGraph* const graph) override;
        virtual void Execute(Rendering::Scenes::SceneData* const scene_data, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
        virtual void Render(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
    };

    struct SkyboxPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph) override;
        virtual void Compile(RenderPasses::RenderPass** pass, RenderGraph* const graph) override;
        virtual void Execute(Rendering::Scenes::SceneData* const scene_data, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
        virtual void Render(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;

    private:
        Hardwares::VertexBufferSetHandle  m_vb_handle   = {};
        Hardwares::IndexBufferSetHandle   m_ib_handle   = {};
        Textures::TextureHandle           m_env_map     = {};
        Core::Containers::Array<uint16_t> m_index_data  = {};
        Core::Containers::Array<float>    m_vertex_data = {};
    };

    struct GridPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph) override;
        virtual void Compile(RenderPasses::RenderPass** pass, RenderGraph* const graph) override;
        virtual void Execute(Rendering::Scenes::SceneData* const scene_data, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
        virtual void Render(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;

    private:
        Core::Containers::Array<uint16_t> m_index_data  = {};
        Core::Containers::Array<float>    m_vertex_data = {};
        Hardwares::VertexBufferSetHandle  m_vb_handle   = {};
        Hardwares::IndexBufferSetHandle   m_ib_handle   = {};
    };

    struct GbufferPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph) override;
        virtual void Compile(RenderPasses::RenderPass** pass, RenderGraph* const graph) override;
        virtual void Execute(Rendering::Scenes::SceneData* const scene_data, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
        virtual void Render(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
    };

    struct LightingPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraph* const graph) override;
        virtual void Compile(RenderPasses::RenderPass** pass, RenderGraph* const graph) override;
        virtual void Execute(Rendering::Scenes::SceneData* const scene_data, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
        virtual void Render(Rendering::Scenes::SceneData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph) override;
    };

} // namespace ZEngine::Rendering::Renderers