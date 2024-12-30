#pragma once
#include <RenderGraph.h>
#include <Rendering/Buffers/IndirectBuffer.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <ZEngineDef.h>
#include <vector>

#define WRITE_BUFFERS_ONCE(frame_index, body)          \
    if (!m_write_once_control.contains(frame_index))   \
    {                                                  \
        body m_write_once_control[frame_index] = true; \
    }

namespace ZEngine::Rendering::Renderers
{
    struct DrawData
    {
        uint32_t TransformIndex{0xFFFFFFFF};
        uint32_t MaterialIndex{0xFFFFFFFF};
        uint32_t VertexOffset;
        uint32_t IndexOffset;
        uint32_t VertexCount;
        uint32_t IndexCount;
    };

    struct IndirectRenderingStorage
    {
        virtual void Initialize(GraphicRenderer* renderer);

    protected:
        std::map<uint32_t, bool>         m_write_once_control;
        std::map<uint32_t, uint32_t>     m_cached_vertices_count;
        std::map<uint32_t, uint32_t>     m_cached_indices_count;
        Buffers::StorageBufferSetHandle  m_vertex_buffer_handle;
        Buffers::StorageBufferSetHandle  m_index_buffer_handle;
        Buffers::StorageBufferSetHandle  m_draw_buffer_handle;
        Buffers::StorageBufferSetHandle  m_transform_buffer_handle;
        Buffers::IndirectBufferSetHandle m_indirect_buffer_handle;
    };

    struct SceneDepthPrePass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Helpers::Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;
        virtual void Render(
            uint32_t                   frame_index,
            RenderPasses::RenderPass*  pass,
            Buffers::FramebufferVNext* framebuffer,
            Buffers::CommandBuffer*    command_buffer,
            RenderGraph*               graph) override;
    };

    struct SkyboxPass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Helpers::Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;
        virtual void Render(
            uint32_t                   frame_index,
            RenderPasses::RenderPass*  pass,
            Buffers::FramebufferVNext* framebuffer,
            Buffers::CommandBuffer*    command_buffer,
            RenderGraph*               graph) override;

    private:
        const std::vector<float> m_vertex_data = {
            -1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0,
            -1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        };
        const std::vector<uint32_t>              m_index_data        = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};
        const std::vector<DrawData>              m_draw_data         = {DrawData{.VertexOffset = 0, .IndexOffset = 0, .VertexCount = 8, .IndexCount = 36}};
        const std::vector<VkDrawIndirectCommand> m_indirect_commmand = {VkDrawIndirectCommand{.vertexCount = 36, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};
    };

    struct GridPass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Helpers::Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;
        virtual void Render(
            uint32_t                   frame_index,
            RenderPasses::RenderPass*  pass,
            Buffers::FramebufferVNext* framebuffer,
            Buffers::CommandBuffer*    command_buffer,
            RenderGraph*               graph) override;

    private:
        const std::vector<float>                 m_vertex_data       = {-1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0,  0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                                        1.0,  0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0};
        const std::vector<uint32_t>              m_index_data        = {0, 1, 2, 2, 3, 0};
        const std::vector<DrawData>              m_draw_data         = {DrawData{.VertexOffset = 0, .IndexOffset = 0, .VertexCount = 4, .IndexCount = 6}};
        const std::vector<VkDrawIndirectCommand> m_indirect_commmand = {VkDrawIndirectCommand{.vertexCount = 6, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};
    };

    struct GbufferPass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Helpers::Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;
        virtual void Render(
            uint32_t                   frame_index,
            RenderPasses::RenderPass*  pass,
            Buffers::FramebufferVNext* framebuffer,
            Buffers::CommandBuffer*    command_buffer,
            RenderGraph*               graph) override;
    };

    struct LightingPass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Helpers::Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;
        virtual void Render(
            uint32_t                   frame_index,
            RenderPasses::RenderPass*  pass,
            Buffers::FramebufferVNext* framebuffer,
            Buffers::CommandBuffer*    command_buffer,
            RenderGraph*               graph) override;
    };

} // namespace ZEngine::Rendering::Renderers