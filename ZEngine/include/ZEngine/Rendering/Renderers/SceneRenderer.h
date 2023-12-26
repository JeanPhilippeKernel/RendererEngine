#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <ZEngineDef.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Renderers/RenderPasses/RenderPass.h>
#include <Rendering/Buffers/IndirectBuffer.h>

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

    struct SceneRenderer : public Helpers::RefCounted
    {
        SceneRenderer()  = default;
        ~SceneRenderer() = default;

        void Initialize(const Ref<Buffers::UniformBufferSet>& camera);
        void Deinitialize();

        void StartScene(const glm::vec3& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection);
        void StartScene(const glm::vec4& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection);
        void StartScene(Buffers::CommandBuffer* const command_buffer);
        void RenderScene(const Ref<Rendering::Scenes::SceneRawData>& scene_data, uint32_t current_frame_index = 0);
        void EndScene(Buffers::CommandBuffer* const command_buffer, uint32_t current_frame_index = 0);
        void SetViewportSize(uint32_t width, uint32_t height);

    private:
        glm::vec4 m_camera_position{1.0f};
        glm::mat4 m_camera_view{1.0f};
        glm::mat4 m_camera_projection{1.0f};
        /*
         * Scene Data Per Frame
         */
        Ref<Buffers::StorageBufferSet>                 m_SBVertex;
        Ref<Buffers::StorageBufferSet>                 m_SBIndex;
        Ref<Buffers::StorageBufferSet>                 m_SBDrawData;
        Ref<Buffers::StorageBufferSet>                 m_SBTransform;
        Ref<Buffers::StorageBufferSet>                 m_SBMaterialData;
        std::vector<Ref<Buffers::IndirectBuffer>>      m_indirect_buffer;
        std::vector<Ref<Rendering::Textures::Texture>> m_global_texture_buffer_collection;
        /*
         * Passes
         */
        Ref<RenderPasses::RenderPass> m_final_color_output_pass;
        /*
         * Infinite Grid pass
         */
        Ref<Buffers::StorageBufferSet>            m_GridSBVertex;
        Ref<Buffers::StorageBufferSet>            m_GridSBIndex;
        Ref<Buffers::StorageBufferSet>            m_GridSBDrawData;
        std::vector<Ref<Buffers::IndirectBuffer>> m_infinite_grid_indirect_buffer;
        Ref<RenderPasses::RenderPass>             m_infinite_grid_pass;
        const std::vector<float>                  m_grid_vertices          = {-1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0,  0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                                              1.0,  0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0};
        const std::vector<uint32_t>               m_grid_indices           = {0, 1, 2, 2, 3, 0};
        const std::vector<DrawData>               m_grid_drawData          = {DrawData{.Index = 0, .VertexOffset = 0, .IndexOffset = 0, .VertexCount = 4, .IndexCount = 6}};
        const std::vector<VkDrawIndirectCommand>  m_grid_indirect_commmand = {VkDrawIndirectCommand{.vertexCount = 6, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};

        /*
         * Cubemap
         */
        Ref<Buffers::StorageBufferSet>            m_CubemapSBVertex;
        Ref<Buffers::StorageBufferSet>            m_CubemapSBIndex;
        Ref<Buffers::StorageBufferSet>            m_CubemapSBDrawData;
        Ref<Textures::Texture>                    m_environment_map;
        Ref<RenderPasses::RenderPass>             m_cubemap_pass;
        std::vector<Ref<Buffers::IndirectBuffer>> m_cubemap_indirect_buffer;
        const std::vector<float>                  m_cubemap_vertex_data = {
            -1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0,
            -1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        };
        const std::vector<uint32_t> m_cubemap_index_data = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};
        const std::vector<DrawData> m_cubmap_draw_data   = {DrawData{.Index = 0, .VertexOffset = 0, .IndexOffset = 0, .VertexCount = 8, .IndexCount = 36}};
        const std::vector<VkDrawIndirectCommand> m_cubemap_indirect_commmand = {VkDrawIndirectCommand{.vertexCount = 36, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};

    private:
        int                   m_upload_once_per_frame_count{-1};
        uint32_t              m_last_uploaded_buffer_image_count{0};
        std::vector<uint32_t> m_last_drawn_vertices_count;
        std::vector<uint32_t> m_last_drawn_index_count;
    };
} // namespace ZEngine::Rendering::Renderers