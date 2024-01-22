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

    struct SceneRenderer : public Helpers::RefCounted
    {
        SceneRenderer()  = default;
        ~SceneRenderer() = default;

        void Initialize(RenderGraph* const graph);
        void Deinitialize();

    private:
        /*
         * Scene Data Per Frame
         */
        static Ref<Buffers::StorageBufferSet>                 m_SBVertex;
        static Ref<Buffers::StorageBufferSet>                 m_SBIndex;
        static Ref<Buffers::StorageBufferSet>                 m_SBDrawData;
        static Ref<Buffers::StorageBufferSet>                 m_SBTransform;
        static Ref<Buffers::StorageBufferSet>                 m_SBMaterialData;
        static Ref<Buffers::IndirectBufferSet>                m_indirect_buffer;
        static std::vector<Ref<Rendering::Textures::Texture>> m_global_texture_buffer_collection;
        static uint32_t                                       m_last_uploaded_buffer_image_count;
        static std::vector<uint32_t>                          m_last_drawn_vertices_count;
        static std::vector<uint32_t>                          m_last_drawn_index_count;
        /*
         * Infinite Grid pass
         */
        static Ref<Buffers::StorageBufferSet>            m_GridSBVertex;
        static Ref<Buffers::StorageBufferSet>            m_GridSBIndex;
        static Ref<Buffers::StorageBufferSet>            m_GridSBDrawData;
        static Ref<Buffers::IndirectBufferSet>           m_infinite_grid_indirect_buffer;
        static const std::vector<float>                  m_grid_vertices;
        static const std::vector<uint32_t>               m_grid_indices;
        static const std::vector<DrawData>               m_grid_drawData;
        static const std::vector<VkDrawIndirectCommand>  m_grid_indirect_commmand;
        /*
         * Cubemap
         */
        static Ref<Buffers::StorageBufferSet>            m_CubemapSBVertex;
        static Ref<Buffers::StorageBufferSet>            m_CubemapSBIndex;
        static Ref<Buffers::StorageBufferSet>            m_CubemapSBDrawData;
        static Ref<Textures::Texture>                    m_environment_map;
        static Ref<Buffers::IndirectBufferSet>           m_cubemap_indirect_buffer;
        static const std::vector<float>                  m_cubemap_vertex_data;
        static const std::vector<uint32_t>               m_cubemap_index_data;
        static const std::vector<DrawData>               m_cubmap_draw_data;
        static const std::vector<VkDrawIndirectCommand>  m_cubemap_indirect_commmand;

    private:
        static void __ExecuteCubemapCallback(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer);
        static void __ExecuteInfiniteGridPassCallback(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer);
        static void __ExecuteFinalPassCallback(
            uint32_t                               frame_index,
            Rendering::Scenes::SceneRawData* const scene_data,
            RenderPasses::RenderPass*              pass,
            Buffers::CommandBuffer*                command_buffer);
    };
} // namespace ZEngine::Rendering::Renderers