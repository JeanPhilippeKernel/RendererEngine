#include <pch.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/SceneRenderer.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Specifications/GraphicRendererPipelineSpecification.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    void SceneRenderer::Initialize()
    {
        const auto& renderer_info = Renderers::GraphicRenderer::GetRendererInformation();
        m_command_pool            = Hardwares::VulkanDevice::CreateCommandPool(QueueType::GRAPHIC_QUEUE, renderer_info.SwapchainIdentifier, true);

        m_last_drawn_vertices_count.resize(renderer_info.FrameCount, 0);
        m_last_drawn_index_count.resize(renderer_info.FrameCount, 0);

        m_UB_Camera      = CreateRef<Buffers::UniformBufferSet>(renderer_info.FrameCount);
        m_SBVertex       = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBIndex        = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBDrawData     = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBTransform    = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_SBMaterialData = CreateRef<Buffers::StorageBufferSet>(renderer_info.FrameCount);
        m_indirect_buffer.resize(renderer_info.FrameCount, CreateRef<Buffers::IndirectBuffer>());
        /*
         * Render Passes definition
         */
        Specifications::GraphicRendererPipelineSpecification final_pipeline_spec = {};
        final_pipeline_spec.DebugName                                            = "Standard-Pipeline";
        final_pipeline_spec.TargetFrameBufferSpecification                       = GraphicRenderer::GetRenderTarget(RenderTarget::FRAME_OUTPUT);
        final_pipeline_spec.ShaderSpecification          = {.VertexFilename = "Shaders/Cache/final_color_vertex.spv", .FragmentFilename = "Shaders/Cache/final_color_fragment.spv"};
        RenderPasses::RenderPassSpecification color_pass = {};
        color_pass.DebugName                             = "Final-Color-Attachment";
        color_pass.Pipeline                              = Pipelines::GraphicPipeline::Create(final_pipeline_spec);
        m_final_color_output_pass                        = RenderPasses::RenderPass::Create(color_pass);

        m_final_color_output_pass->SetInput("UBCamera", m_UB_Camera);
        m_final_color_output_pass->SetInput("VertexSB", m_SBVertex);
        m_final_color_output_pass->SetInput("IndexSB", m_SBIndex);
        m_final_color_output_pass->SetInput("DrawDataSB", m_SBDrawData);
        m_final_color_output_pass->SetInput("TransformSB", m_SBTransform);
        m_final_color_output_pass->SetInput("MatSB", m_SBMaterialData);

        m_final_color_output_pass->Verify();
        m_final_color_output_pass->Bake();
    }

    void SceneRenderer::Deinitialize()
    {
        m_final_color_output_pass->Dispose();

        m_UB_Camera->Dispose();
        m_SBVertex->Dispose();
        m_SBIndex->Dispose();
        m_SBDrawData->Dispose();
        m_SBTransform->Dispose();
        m_SBMaterialData->Dispose();

        m_global_texture_buffer_collection.clear();
        m_global_texture_buffer_collection.shrink_to_fit();

        Hardwares::VulkanDevice::DisposeCommandPool(m_command_pool);
    }

    void SceneRenderer::StartScene(const glm::vec4& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection)
    {
        m_camera_position   = camera_position;
        m_camera_view       = camera_view;
        m_camera_projection = camera_projection;
    }

    void SceneRenderer::StartScene(const glm::vec3& camera_position, const glm::mat4& camera_view, const glm::mat4& camera_projection)
    {
        this->StartScene(glm::vec4(camera_position, 1.0f), camera_view, camera_projection);
    }

    void SceneRenderer::RenderScene(const Ref<Rendering::Scenes::SceneRawData>& scene_data)
    {
        uint32_t current_frame_index = Renderers::GraphicRenderer::GetRendererInformation().CurrentFrameIndex;
        /*
         * Uploading Camera info
         * Todo: Can be multithreaded
         */
        auto& scene_camera    = *m_UB_Camera;
        auto  ubo_camera_data = Contracts::UBOCameraLayout{.View = m_camera_view, .Projection = m_camera_projection, .Position = m_camera_position};
        scene_camera[current_frame_index].SetData(&ubo_camera_data, sizeof(Contracts::UBOCameraLayout));

        const auto& sceneNodeMeshMap = scene_data->SceneNodeMeshMap;
        /*
         * Composing Transform Data
         */
        std::vector<glm::mat4> tranform_collection = {};
        for (const auto& sceneNodeMeshPair : sceneNodeMeshMap)
        {
            tranform_collection.push_back(scene_data->GlobalTransformCollection[sceneNodeMeshPair.first]);
        }
        auto& transform_storage = *m_SBTransform;
        transform_storage[current_frame_index].SetData(tranform_collection);

        /*
         * Scenes Textures
         */
        if (m_last_uploaded_buffer_image_count != scene_data->TextureCollection->Size())
        {
            auto& scene_textures_collection = scene_data->TextureCollection;
            m_final_color_output_pass->SetInput("TextureArray", scene_textures_collection);
            m_last_uploaded_buffer_image_count = scene_textures_collection->Size();

            m_final_color_output_pass->MarkDirty();
        }
        /*
         * Scene Draw data
         */
        if ((m_last_drawn_vertices_count[current_frame_index] == scene_data->Vertices.size()) || (m_last_drawn_index_count[current_frame_index] == scene_data->Indices.size()))
        {
            return;
        }

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
        auto& vertex_storage = *m_SBVertex;
        auto& index_storage  = *m_SBIndex;
        vertex_storage[current_frame_index].SetData(scene_data->Vertices);
        index_storage[current_frame_index].SetData(scene_data->Indices);
        /*
         * Uploading Drawing data
         */
        auto& draw_data_storage = *m_SBDrawData;
        draw_data_storage[current_frame_index].SetData(draw_data_collection);
        /*
         * Uploading Material data
         */
        auto& material_data_storage = *m_SBMaterialData;
        material_data_storage[current_frame_index].SetData(material_collection);
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

        m_indirect_buffer[current_frame_index]->SetData(draw_indirect_commmand);

        /*
         * Caching last vertex/index count per frame
         */
        m_last_drawn_vertices_count[current_frame_index] = scene_data->Vertices.size();
        m_last_drawn_index_count[current_frame_index]    = scene_data->Indices.size();

        /*
         * Mark RenderPass dirty and should re-upadte inputs
         */
        m_final_color_output_pass->MarkDirty();
    }

    void SceneRenderer::EndScene()
    {
        uint32_t current_frame_index = Renderers::GraphicRenderer::GetRendererInformation().CurrentFrameIndex;

        auto command_buffer = m_command_pool->GetCurrentCommmandBuffer();
        /*
         * Drawing the scene...
         */
        command_buffer->Begin();
        {
            command_buffer->BeginRenderPass(m_final_color_output_pass);
            command_buffer->BindDescriptorSets(current_frame_index);
            command_buffer->DrawIndirect(m_indirect_buffer[current_frame_index]);
            command_buffer->EndRenderPass();
        }
        command_buffer->End();
        command_buffer->Submit();
    }

    void SceneRenderer::Tick()
    {
        m_command_pool->Tick();
    }
} // namespace ZEngine::Rendering::Renderers
