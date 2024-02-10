#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <ZEngineDef.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Buffers/IndirectBuffer.h>

#include <Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    struct DrawData
    {
        uint32_t Index{0xFFFFFFFF};
        uint32_t TransformIndex{0xFFFFFFFF};
        uint32_t MaterialIndex{0xFFFFFFFF};
        uint32_t VertexOffset;
        uint32_t IndexOffset;
        uint32_t VertexCount;
        uint32_t IndexCount;
    };

    struct IndirectRenderingStorage
    {
        virtual void Initialize(uint32_t count);
        virtual void Dispose();

    protected:
        std::map<uint32_t, bool>        m_write_once_control;
        Ref<Buffers::StorageBufferSet>  m_vertex_buffer;
        Ref<Buffers::StorageBufferSet>  m_index_buffer;
        Ref<Buffers::StorageBufferSet>  m_draw_buffer;
        Ref<Buffers::StorageBufferSet>  m_transform_buffer;
        Ref<Buffers::IndirectBufferSet> m_indirect_buffer;
    };

    /*
     * Passes definition
     */
    struct SceneDepthPrePass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;

    private:
        std::map<uint32_t, uint32_t> m_cached_vertices_count;
        std::map<uint32_t, uint32_t> m_cached_indices_count;
    };

    struct CubemapPass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {

        virtual void Dispose() override
        {
            IndirectRenderingStorage::Dispose();
            m_environment_map->Dispose();
        }

        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;

    private:
        const std::vector<float> m_vertex_data = {
            -1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0,
            -1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        };
        const std::vector<uint32_t>              m_index_data        = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};
        const std::vector<DrawData>              m_draw_data         = {DrawData{.Index = 0, .VertexOffset = 0, .IndexOffset = 0, .VertexCount = 8, .IndexCount = 36}};
        const std::vector<VkDrawIndirectCommand> m_indirect_commmand = {VkDrawIndirectCommand{.vertexCount = 36, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};
        Ref<Textures::Texture>                   m_environment_map;
    };

    struct GridPass : public IRenderGraphCallbackPass, public IndirectRenderingStorage
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;

    private:
        const std::vector<float>                 m_vertex_data       = {-1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0,  0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                                        1.0,  0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0};
        const std::vector<uint32_t>              m_index_data        = {0, 1, 2, 2, 3, 0};
        const std::vector<DrawData>              m_draw_data         = {DrawData{.Index = 0, .VertexOffset = 0, .IndexOffset = 0, .VertexCount = 4, .IndexCount = 6}};
        const std::vector<VkDrawIndirectCommand> m_indirect_commmand = {VkDrawIndirectCommand{.vertexCount = 6, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};
    };

    struct GbufferPass : public IRenderGraphCallbackPass
    {
        virtual void Setup(std::string_view name, RenderGraphBuilder* const builder) override;
        virtual void Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph) override;
        virtual void Execute(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer,
            RenderGraph* const                     graph) override;

    private:
        std::map<uint32_t, uint32_t> m_cached_vertices_count;
        std::map<uint32_t, uint32_t> m_cached_indices_count;
    };

    struct SceneRenderer : public Helpers::RefCounted
    {
        SceneRenderer()  = default;
        ~SceneRenderer() = default;

        void Initialize(RenderGraph* const graph);
        void Deinitialize();

    private:
        Ref<SceneDepthPrePass> m_scene_depth_prepass;
        Ref<CubemapPass>       m_cubemap_pass;
        Ref<GridPass>          m_grid_pass;
        Ref<GbufferPass>       m_gbuffer_pass;
    };
} // namespace ZEngine::Rendering::Renderers