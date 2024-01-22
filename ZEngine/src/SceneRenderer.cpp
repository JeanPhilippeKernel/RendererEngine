#include <pch.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/SceneRenderer.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Textures/Texture2D.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_SBVertex;
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_SBIndex;
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_SBDrawData;
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_SBTransform;
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_SBMaterialData;
    Ref<Buffers::IndirectBufferSet>                SceneRenderer::m_indirect_buffer;
    std::vector<Ref<Rendering::Textures::Texture>> SceneRenderer::m_global_texture_buffer_collection;
    uint32_t                                       SceneRenderer::m_last_uploaded_buffer_image_count{0};
    std::vector<uint32_t>                          SceneRenderer::m_last_drawn_vertices_count;
    std::vector<uint32_t>                          SceneRenderer::m_last_drawn_index_count;
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_GridSBVertex;
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_GridSBIndex;
    Ref<Buffers::StorageBufferSet>                 SceneRenderer::m_GridSBDrawData;
    Ref<Buffers::IndirectBufferSet>                SceneRenderer::m_infinite_grid_indirect_buffer;
    const std::vector<float>                       SceneRenderer::m_grid_vertices = {-1.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0,  0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                                                                     1.0,  0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0};
    const std::vector<uint32_t>                    SceneRenderer::m_grid_indices  = {0, 1, 2, 2, 3, 0};
    const std::vector<DrawData>                    SceneRenderer::m_grid_drawData = {DrawData{.Index = 0, .VertexOffset = 0, .IndexOffset = 0, .VertexCount = 4, .IndexCount = 6}};
    const std::vector<VkDrawIndirectCommand>       SceneRenderer::m_grid_indirect_commmand = {
        VkDrawIndirectCommand{.vertexCount = 6, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};
    Ref<Buffers::StorageBufferSet>  SceneRenderer::m_CubemapSBVertex;
    Ref<Buffers::StorageBufferSet>  SceneRenderer::m_CubemapSBIndex;
    Ref<Buffers::StorageBufferSet>  SceneRenderer::m_CubemapSBDrawData;
    Ref<Textures::Texture>          SceneRenderer::m_environment_map;
    Ref<Buffers::IndirectBufferSet> SceneRenderer::m_cubemap_indirect_buffer;
    const std::vector<float>        SceneRenderer::m_cubemap_vertex_data = {
        -1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, 1.0,  0.0, 0.0, 0.0, 0.0, 0.0,
        -1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, -1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 1.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    };
    const std::vector<uint32_t> SceneRenderer::m_cubemap_index_data = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};
    const std::vector<DrawData> SceneRenderer::m_cubmap_draw_data   = {DrawData{.Index = 0, .VertexOffset = 0, .IndexOffset = 0, .VertexCount = 8, .IndexCount = 36}};
    const std::vector<VkDrawIndirectCommand> SceneRenderer::m_cubemap_indirect_commmand = {
        VkDrawIndirectCommand{.vertexCount = 36, .instanceCount = 1, .firstVertex = 0, .firstInstance = 0}};

    void SceneRenderer::Initialize(RenderGraph* const graph)
    {
        const auto& renderer_info = Renderers::GraphicRenderer::GetRendererInformation();

        // m_upload_once_per_frame_count = renderer_info.FrameCount;
        m_last_drawn_vertices_count.resize(renderer_info.FrameCount, 0);
        m_last_drawn_index_count.resize(renderer_info.FrameCount, 0);

        m_CubemapSBVertex         = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_CubemapSBIndex          = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_CubemapSBDrawData       = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_cubemap_indirect_buffer = CreateRef<Buffers::IndirectBufferSet>(renderer_info.FrameCount);
        m_environment_map         = Textures::Texture2D::ReadCubemap("Settings/EnvironmentMaps/piazza_bologni_1k.hdr");

        m_GridSBVertex                  = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_GridSBIndex                   = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_GridSBDrawData                = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_infinite_grid_indirect_buffer = CreateRef<Buffers::IndirectBufferSet>(renderer_info.FrameCount);

        m_SBVertex        = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBIndex         = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBDrawData      = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBTransform     = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBMaterialData  = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_indirect_buffer = CreateRef<Buffers::IndirectBufferSet>(renderer_info.FrameCount);

        graph->AddCallbackPass(
            "Scene Depth Pre-Pass",
            [&](std::string_view name, RenderGraphBuilder* const builder) {
                builder->AttachBuffer("scene_vertex_buffer", m_SBVertex);
                builder->AttachBuffer("scene_index_buffer", m_SBIndex);
                builder->AttachBuffer("scene_draw_buffer", m_SBDrawData);
                builder->AttachBuffer("scene_transform_buffer", m_SBTransform);
                builder->AttachBuffer("scene_material_buffer", m_SBMaterialData);

                Specifications::TextureSpecification output_depth_desc = {};
                output_depth_desc.Format                               = ImageFormat::DEPTH_STENCIL_FROM_DEVICE;
                output_depth_desc.LoadOp                               = LoadOperation::CLEAR;
                output_depth_desc.Width                                = 1280;
                output_depth_desc.Height                               = 780;
                auto& output_depth                                     = builder->CreateRenderTarget("Depth", output_depth_desc);

                RenderGraphRenderPassCreation pass_node = {};
                pass_node.Name                          = name.data();
                pass_node.Outputs.push_back({.Name = "Depth"});
                builder->CreateRenderPassNode(pass_node);
            },
            [&](Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder) {
                handle = builder.SetPipelineName("Final-Color-Pipeline").UseShader("depth_prepass").Create();
                // handle = builder.SetPipelineName("Final-Color-Pipeline").UseShader("final_scene").Create();

                handle->SetInput("UBCamera", graph->GetResource("scene_camera").ResourceInfo.UniformBufferSetHandle);
                handle->SetInput("VertexSB", graph->GetResource("scene_vertex_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("IndexSB", graph->GetResource("scene_index_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("DrawDataSB", graph->GetResource("scene_draw_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("TransformSB", graph->GetResource("scene_transform_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("MatSB", graph->GetResource("scene_material_buffer").ResourceInfo.BufferSetHandle);
                handle->Verify();
                handle->Bake();
            },
            __ExecuteFinalPassCallback);

        graph->AddCallbackPass(
            "Depth Cubemap Pre-Pass",
            [&](std::string_view name, RenderGraphBuilder* const builder) {
                builder->AttachBuffer("cubemap_vertex_buffer", m_CubemapSBVertex);
                builder->AttachBuffer("cubemap_index_buffer", m_CubemapSBIndex);
                builder->AttachBuffer("cubemap_draw_buffer", m_CubemapSBDrawData);
                builder->AttachTexture("cubemap_env_map", m_environment_map);

                RenderGraphRenderPassCreation pass_node_creation = {};
                pass_node_creation.Name                          = name.data();
                pass_node_creation.Inputs.push_back({.Name = "Depth"});
                pass_node_creation.Outputs.push_back({.Name = "Depth", .Type = RenderGraphResourceType::REFERENCE});
                builder->CreateRenderPassNode(pass_node_creation);
            },
            [&](Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder) {
                handle = builder.SetPipelineName("Cubemap-Pipeline").UseShader("cubemap").Create();

                handle->SetInput("UBCamera", graph->GetResource("scene_camera").ResourceInfo.UniformBufferSetHandle);
                handle->SetInput("VertexSB", graph->GetResource("cubemap_vertex_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("IndexSB", graph->GetResource("cubemap_index_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("DrawDataSB", graph->GetResource("cubemap_draw_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("CubemapTexture", graph->GetResource("cubemap_env_map").ResourceInfo.TextureHandle);
                handle->Verify();
                handle->Bake();
            },
            __ExecuteCubemapCallback);

        graph->AddCallbackPass(
            "Depth Grid Pre-Pass",
            [&](std::string_view name, RenderGraphBuilder* const builder) {
                builder->AttachBuffer("infinite_grid_vertex_buffer", m_GridSBVertex);
                builder->AttachBuffer("infinite_grid_index_buffer", m_GridSBIndex);
                builder->AttachBuffer("infinite_grid_draw_buffer", m_GridSBDrawData);

                RenderGraphRenderPassCreation pass_node = {};
                pass_node.Name                          = name.data();
                pass_node.Inputs.push_back({.Name = "Depth"});
                pass_node.Outputs.push_back({.Name = "Depth", .Type = RenderGraphResourceType::REFERENCE});
                builder->CreateRenderPassNode(pass_node);
            },
            [&](Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder) {
                handle = builder.SetPipelineName("Infinite-Grid-Pipeline").UseShader("infinite_grid").Create();

                handle->SetInput("UBCamera", graph->GetResource("scene_camera").ResourceInfo.UniformBufferSetHandle);
                handle->SetInput("VertexSB", graph->GetResource("infinite_grid_vertex_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("IndexSB", graph->GetResource("infinite_grid_index_buffer").ResourceInfo.BufferSetHandle);
                handle->SetInput("DrawDataSB", graph->GetResource("infinite_grid_draw_buffer").ResourceInfo.BufferSetHandle);
                handle->Verify();
                handle->Bake();
            },
            __ExecuteInfiniteGridPassCallback);
    }

    void SceneRenderer::Deinitialize()
    {
        m_environment_map->Dispose();
        m_CubemapSBVertex->Dispose();
        m_CubemapSBIndex->Dispose();
        m_CubemapSBDrawData->Dispose();
        m_cubemap_indirect_buffer->Dispose();

        m_GridSBVertex->Dispose();
        m_GridSBIndex->Dispose();
        m_GridSBDrawData->Dispose();
        m_infinite_grid_indirect_buffer->Dispose();

        m_SBVertex->Dispose();
        m_SBIndex->Dispose();
        m_SBDrawData->Dispose();
        m_SBTransform->Dispose();
        m_SBMaterialData->Dispose();
        m_indirect_buffer->Dispose();

        m_global_texture_buffer_collection.clear();
        m_global_texture_buffer_collection.shrink_to_fit();
    }

    void SceneRenderer::__ExecuteCubemapCallback(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer)
    {
        /*
         * Uploading Cubemap
         */
        {
            m_CubemapSBVertex->SetData<float>(frame_index, m_cubemap_vertex_data);
            m_CubemapSBIndex->SetData<uint32_t>(frame_index, m_cubemap_index_data);
            m_CubemapSBDrawData->SetData<DrawData>(frame_index, m_cubmap_draw_data);
            m_cubemap_indirect_buffer->SetData(frame_index, m_cubemap_indirect_commmand);
        }

        pass->MarkDirty();
        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(m_cubemap_indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void SceneRenderer::__ExecuteInfiniteGridPassCallback(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer)
    {
        /*
         * Uploading Infinite Grid
         */
        {
            m_GridSBVertex->SetData<float>(frame_index, m_grid_vertices);
            m_GridSBIndex->SetData<uint32_t>(frame_index, m_grid_indices);
            m_GridSBDrawData->SetData<DrawData>(frame_index, m_grid_drawData);
            m_infinite_grid_indirect_buffer->SetData(frame_index, m_grid_indirect_commmand);
        }

        pass->MarkDirty();
        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(m_infinite_grid_indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void SceneRenderer::__ExecuteFinalPassCallback(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer)
    {
        const auto& sceneNodeMeshMap = scene_data->SceneNodeMeshMap;
        /*
         * Composing Transform Data
         */
        std::vector<glm::mat4> tranform_collection = {};
        for (const auto& sceneNodeMeshPair : sceneNodeMeshMap)
        {
            tranform_collection.push_back(scene_data->GlobalTransformCollection[sceneNodeMeshPair.first]);
        }

        m_SBTransform->SetData<glm::mat4>(frame_index, tranform_collection);

        /*
         * Scenes Textures
         */
        if (m_last_uploaded_buffer_image_count != scene_data->TextureCollection->Size())
        {
            auto& scene_textures_collection = scene_data->TextureCollection;
            pass->SetInput("TextureArray", scene_textures_collection);
            m_last_uploaded_buffer_image_count = scene_textures_collection->Size();

            pass->MarkDirty();
        }
        /*
         * Scene Draw data
         */
        bool perform_draw_update = true;
        if ((m_last_drawn_vertices_count[frame_index] == scene_data->Vertices.size()) || (m_last_drawn_index_count[frame_index] == scene_data->Indices.size()))
        {
            perform_draw_update = false;
        }

        if (perform_draw_update)
        {
            std::vector<DrawData>             draw_data_collection = {};
            std::vector<Meshes::MeshMaterial> material_collection  = {};

            uint32_t data_index = 0;
            for (const auto& sceneNodeMeshPair : sceneNodeMeshMap)
            {
                /*
                 * Composing DrawData
                 */
                DrawData& draw_data      = draw_data_collection.emplace_back(DrawData{.Index = data_index});
                draw_data.TransformIndex = data_index;
                draw_data.VertexOffset   = sceneNodeMeshPair.second.VertexOffset;
                draw_data.IndexOffset    = sceneNodeMeshPair.second.IndexOffset;
                draw_data.VertexCount    = sceneNodeMeshPair.second.VertexCount;
                draw_data.IndexCount     = sceneNodeMeshPair.second.IndexCount;
                /*
                 * Material data
                 */
                material_collection.push_back(scene_data->SceneNodeMaterialMap[sceneNodeMeshPair.first]);
                draw_data.MaterialIndex = material_collection.size() - 1;

                data_index++;
            }
            /*
             * Uploading Geometry data
             */
            m_SBVertex->SetData<float>(frame_index, scene_data->Vertices);
            m_SBIndex->SetData<uint32_t>(frame_index, scene_data->Indices);
            /*
             * Uploading Drawing data
             */
            m_SBDrawData->SetData<DrawData>(frame_index, draw_data_collection);
            /*
             * Uploading Material data
             */
            auto& material_data_storage = *m_SBMaterialData;
            m_SBMaterialData->SetData<Meshes::MeshMaterial>(frame_index, material_collection);
            /*
             * Uploading Indirect Commands
             */
            std::vector<VkDrawIndirectCommand> draw_indirect_commmand = {};
            draw_indirect_commmand.resize(draw_data_collection.size());
            for (uint32_t i = 0; i < draw_indirect_commmand.size(); ++i)
            {
                draw_indirect_commmand[i] = {
                    .vertexCount   = draw_data_collection[i].IndexCount,
                    .instanceCount = 1,
                    .firstVertex   = 0,
                    .firstInstance = i,
                };
            }

            m_indirect_buffer->SetData(frame_index, draw_indirect_commmand);

            /*
             * Caching last vertex/index count per frame
             */
            m_last_drawn_vertices_count[frame_index] = scene_data->Vertices.size();
            m_last_drawn_index_count[frame_index]    = scene_data->Indices.size();

            /*
             * Mark RenderPass dirty and should re-upadte inputs
             */
            pass->MarkDirty();
        }

        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(m_indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

} // namespace ZEngine::Rendering::Renderers
