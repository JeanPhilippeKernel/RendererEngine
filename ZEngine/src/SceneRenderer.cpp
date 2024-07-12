#include <pch.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/SceneRenderer.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Textures/Texture2D.h>


#define WRITE_BUFFERS_ONCE(frame_index, body)           \
    if (!m_write_once_control.contains(frame_index))    \
    {                                                   \
        body m_write_once_control[frame_index] = true;  \
    }

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    void IndirectRenderingStorage::Initialize(uint32_t count)
    {
        m_vertex_buffer    = CreateRef<Buffers::StorageBufferSet>(count);
        m_index_buffer     = CreateRef<Buffers::StorageBufferSet>(count);
        m_draw_buffer      = CreateRef<Buffers::StorageBufferSet>(count);
        m_transform_buffer = CreateRef<Buffers::StorageBufferSet>(count);
        m_indirect_buffer  = CreateRef<Buffers::IndirectBufferSet>(count);
    }

    void IndirectRenderingStorage::Dispose()
    {
        m_vertex_buffer->Dispose();
        m_index_buffer->Dispose();
        m_draw_buffer->Dispose();
        m_transform_buffer->Dispose();
        m_indirect_buffer->Dispose();
    }

    void SceneRenderer::Initialize(RenderGraph* const graph)
    {
        const auto& renderer_info = Renderers::GraphicRenderer::GetRendererInformation();

        m_scene_depth_prepass = CreateRef<SceneDepthPrePass>();
        m_skybox_pass         = CreateRef<SkyboxPass>();
        m_grid_pass           = CreateRef<GridPass>();
        m_gbuffer_pass        = CreateRef<GbufferPass>();
        m_lighting_pass       = CreateRef<LightingPass>();

        m_scene_depth_prepass->Initialize(renderer_info.FrameCount);
        m_skybox_pass->Initialize(renderer_info.FrameCount);
        m_grid_pass->Initialize(renderer_info.FrameCount);
        m_lighting_pass->Initialize(renderer_info.FrameCount);

        graph->AddCallbackPass("Scene Depth Pre-Pass", m_scene_depth_prepass);
        graph->AddCallbackPass("Skybox Pass", m_skybox_pass);
        graph->AddCallbackPass("Grid Pass", m_grid_pass);
        graph->AddCallbackPass("G-Buffer Pass", m_gbuffer_pass);
        graph->AddCallbackPass("Lighting Pass", m_lighting_pass);

        TextureSpecification scene_render_target_spec = {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        auto                 builder                  = graph->GetBuilder();
        builder->CreateRenderTarget("g_scene_render_target", scene_render_target_spec);
        builder->CreateBufferSet("g_scene_vertex_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_index_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_draw_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_transform_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_material_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_directional_light_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_point_light_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_spot_light_buffer", renderer_info.FrameCount);
        builder->CreateBufferSet("g_scene_indirect_buffer", renderer_info.FrameCount, BufferSetCreationType::INDIRECT);
    }

    void SceneRenderer::Deinitialize()
    {
        m_scene_depth_prepass->Dispose();
        m_skybox_pass->Dispose();
        m_grid_pass->Dispose();
        m_lighting_pass->Dispose();
    }

    void SceneDepthPrePass::Setup(std::string_view name, RenderGraphBuilder* const builder)
    {
        Specifications::TextureSpecification output_depth_desc = {.Width = 1280, .Height = 780, .Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE};
        auto&                                output_depth      = builder->CreateRenderTarget("depth_prepass_render_target", output_depth_desc);
        RenderGraphRenderPassCreation        pass_node         = {.Name = name.data(), .Outputs = {{.Name = output_depth.Name}}};
        builder->CreateRenderPassNode(pass_node);
    }

    void SceneDepthPrePass::Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph)
    {
        handle = builder.SetPipelineName("Depth-Prepass-Pipeline").EnablePipelineDepthTest(true).UseShader("depth_prepass_scene").Create();

        auto camera_buffer    = graph.GetBufferUniformSet("scene_camera");
        auto vertex_buffer    = graph.GetBufferSet("g_scene_vertex_buffer");
        auto index_buffer     = graph.GetBufferSet("g_scene_index_buffer");
        auto draw_buffer      = graph.GetBufferSet("g_scene_draw_buffer");
        auto transform_buffer = graph.GetBufferSet("g_scene_transform_buffer");

        handle->SetInput("UBCamera", camera_buffer);
        handle->SetInput("VertexSB", vertex_buffer);
        handle->SetInput("IndexSB", index_buffer);
        handle->SetInput("DrawDataSB", draw_buffer);
        handle->SetInput("TransformSB", transform_buffer);

        handle->Verify();
        handle->Bake();
    }

    void SceneDepthPrePass::Execute(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer,
        RenderGraph* const                     graph)
    {
        auto vertex_buffer            = graph->GetBufferSet("g_scene_vertex_buffer");
        auto index_buffer             = graph->GetBufferSet("g_scene_index_buffer");
        auto draw_buffer              = graph->GetBufferSet("g_scene_draw_buffer");
        auto transform_buffer         = graph->GetBufferSet("g_scene_transform_buffer");
        auto material_buffer          = graph->GetBufferSet("g_scene_material_buffer");
        auto directional_light_buffer = graph->GetBufferSet("g_scene_directional_light_buffer");
        auto point_light_buffer       = graph->GetBufferSet("g_scene_point_light_buffer");
        auto spot_light_buffer        = graph->GetBufferSet("g_scene_spot_light_buffer");
        auto indirect_buffer          = graph->GetIndirectBufferSet("g_scene_indirect_buffer");

        /*
         * Composing Transform Data
         */
        transform_buffer->SetData<glm::mat4>(frame_index, scene_data->GlobalTransformCollection);

        /*
         * Scene Draw data
         */
        bool perform_draw_update = true;
        if ((m_cached_vertices_count[frame_index] == scene_data->Vertices.size()) || (m_cached_indices_count[frame_index] == scene_data->Indices.size()))
        {
            perform_draw_update = false;
        }

        if (perform_draw_update)
        {
            std::vector<DrawData> draw_data_collection = {};
            for (auto& [node, mesh] : scene_data->NodeMeshes)
            {
                /*
                 * Composing DrawData
                 */
                DrawData& draw_data      = draw_data_collection.emplace_back();
                draw_data.TransformIndex = node;
                draw_data.MaterialIndex  = scene_data->NodeMaterials[node];
                draw_data.VertexOffset   = scene_data->Meshes[mesh].VertexOffset;
                draw_data.IndexOffset    = scene_data->Meshes[mesh].IndexOffset;
                draw_data.VertexCount    = scene_data->Meshes[mesh].VertexCount;
                draw_data.IndexCount     = scene_data->Meshes[mesh].IndexCount;
            }
            /*
             * Uploading Geometry data
             */
            vertex_buffer->SetData<float>(frame_index, scene_data->Vertices);
            index_buffer->SetData<uint32_t>(frame_index, scene_data->Indices);
            /*
             * Uploading Drawing data
             */
            draw_buffer->SetData<DrawData>(frame_index, draw_data_collection);
            /*
             * Uploading Material data
             */
            material_buffer->SetData<Meshes::MeshMaterial>(frame_index, scene_data->Materials);
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

            indirect_buffer->SetData(frame_index, draw_indirect_commmand);

            /*
             * Caching last vertex/index count per frame
             */
            m_cached_vertices_count[frame_index] = scene_data->Vertices.size();
            m_cached_indices_count[frame_index]  = scene_data->Indices.size();

            /*
             * Mark RenderPass dirty and should re-upadte inputs
             */
            pass->MarkDirty();
        }

        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void SkyboxPass::Setup(std::string_view name, RenderGraphBuilder* const builder)
    {
        m_environment_map                       = Textures::Texture2D::ReadCubemap("Settings/EnvironmentMaps/piazza_bologni_4k.hdr");
        RenderGraphRenderPassCreation pass_node = {.Name = name.data(), .Inputs = {{.Name = "depth_prepass_render_target"}, {.Name = "lighting_render_target"}}};
        builder->CreateRenderPassNode(pass_node);
    }

    void SkyboxPass::Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph)
    {
        handle = builder.SetPipelineName("Skybox-Pipeline").EnablePipelineDepthTest(true).EnablePipelineDepthWrite(false).UseShader("skybox").Create();

        auto camera_buffer = graph.GetBufferUniformSet("scene_camera");

        handle->SetInput("UBCamera", camera_buffer);
        handle->SetInput("VertexSB", m_vertex_buffer);
        handle->SetInput("IndexSB", m_index_buffer);
        handle->SetInput("DrawDataSB", m_draw_buffer);
        handle->SetInput("TransformSB", m_transform_buffer);
        handle->SetInput("CubemapTexture", m_environment_map);
        handle->Verify();
        handle->Bake();
    }

    void SkyboxPass::Execute(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer,
        RenderGraph* const                     graph)
    {
        WRITE_BUFFERS_ONCE(frame_index, {
            m_vertex_buffer->SetData<float>(frame_index, m_vertex_data);
            m_index_buffer->SetData<uint32_t>(frame_index, m_index_data);
            m_draw_buffer->SetData<DrawData>(frame_index, m_draw_data);
            m_transform_buffer->SetData<glm::mat4>(frame_index, std::vector<glm::mat4>{glm::identity<glm::mat4>()});
            m_indirect_buffer->SetData(frame_index, m_indirect_commmand);

            pass->MarkDirty();
        })
        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(m_indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void GridPass::Setup(std::string_view name, RenderGraphBuilder* const builder)
    {
        RenderGraphRenderPassCreation pass_node = {.Name = name.data(), .Inputs = {{.Name = "depth_prepass_render_target"}, {.Name = "lighting_render_target"}}};
        builder->CreateRenderPassNode(pass_node);
    }

    void GridPass::Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph)
    {
        handle = builder.SetPipelineName("Infinite-Grid-Pipeline").EnablePipelineDepthTest(true).UseShader("infinite_grid").Create();

        auto camera_buffer = graph.GetBufferUniformSet("scene_camera");

        handle->SetInput("UBCamera", camera_buffer);
        handle->SetInput("VertexSB", m_vertex_buffer);
        handle->SetInput("IndexSB", m_index_buffer);
        handle->SetInput("DrawDataSB", m_draw_buffer);
        handle->SetInput("TransformSB", m_transform_buffer);
        handle->Verify();
        handle->Bake();
    }

    void GridPass::Execute(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer,
        RenderGraph* const                     graph)
    {
        WRITE_BUFFERS_ONCE(frame_index, {
            m_vertex_buffer->SetData<float>(frame_index, m_vertex_data);
            m_index_buffer->SetData<uint32_t>(frame_index, m_index_data);
            m_draw_buffer->SetData<DrawData>(frame_index, m_draw_data);
            m_transform_buffer->SetData<glm::mat4>(frame_index, std::vector<glm::mat4>{glm::identity<glm::mat4>()});
            m_indirect_buffer->SetData(frame_index, m_indirect_commmand);

            pass->MarkDirty();
        })
        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(m_indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void GbufferPass::Setup(std::string_view name, RenderGraphBuilder* const builder)
    {
        Specifications::TextureSpecification normal_output_spec   = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification position_output_spec = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification specular_output_spec = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        Specifications::TextureSpecification colour_output_spec   = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};

        auto& gbuffer_albedo   = builder->CreateRenderTarget("gbuffer_albedo_render_target", colour_output_spec);
        auto& gbuffer_specular = builder->CreateRenderTarget("gbuffer_specular_render_target", specular_output_spec);
        auto& gbuffer_normals  = builder->CreateRenderTarget("gbuffer_normals_render_target", normal_output_spec);
        auto& gbuffer_position = builder->CreateRenderTarget("gbuffer_position_render_target", position_output_spec);

        RenderGraphRenderPassCreation pass_node = {};
        pass_node.Name                          = name.data();
        pass_node.Inputs.push_back({.Name = "depth_prepass_render_target"});
        pass_node.Outputs.push_back({.Name = gbuffer_albedo.Name});
        pass_node.Outputs.push_back({.Name = gbuffer_specular.Name});
        pass_node.Outputs.push_back({.Name = gbuffer_normals.Name});
        pass_node.Outputs.push_back({.Name = gbuffer_position.Name});
        builder->CreateRenderPassNode(pass_node);
    }

    void GbufferPass::Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph)
    {
        handle = builder.SetPipelineName("GBuffer-Pipeline").EnablePipelineDepthTest(true).UseShader("g_buffer").Create();

        auto camera_buffer    = graph.GetBufferUniformSet("scene_camera");
        auto vertex_buffer    = graph.GetBufferSet("g_scene_vertex_buffer");
        auto index_buffer     = graph.GetBufferSet("g_scene_index_buffer");
        auto draw_buffer      = graph.GetBufferSet("g_scene_draw_buffer");
        auto transform_buffer = graph.GetBufferSet("g_scene_transform_buffer");
        auto material_buffer  = graph.GetBufferSet("g_scene_material_buffer");

        handle->SetInput("UBCamera", camera_buffer);
        handle->SetInput("VertexSB", vertex_buffer);
        handle->SetInput("IndexSB", index_buffer);
        handle->SetInput("DrawDataSB", draw_buffer);
        handle->SetInput("TransformSB", transform_buffer);
        handle->SetInput("MatSB", material_buffer);
        handle->Verify();
        handle->Bake();
    }

    void GbufferPass::Execute(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer,
        RenderGraph* const                     graph)
    {
        auto indirect_buffer = graph->GetIndirectBufferSet("g_scene_indirect_buffer");

        if ((m_cached_vertices_count[frame_index] != scene_data->Vertices.size()) || (m_cached_indices_count[frame_index] != scene_data->Indices.size()))
        {
            pass->MarkDirty();
            m_cached_vertices_count[frame_index] = scene_data->Vertices.size();
            m_cached_indices_count[frame_index]  = scene_data->Indices.size();
        }

        GraphicRenderer::BindGlobalTextures(pass);

        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void LightingPass::Setup(std::string_view name, RenderGraphBuilder* const builder)
    {
        Specifications::TextureSpecification lighting_output_spec = {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        auto&                                lighting_output      = builder->CreateRenderTarget("lighting_render_target", lighting_output_spec);
        RenderGraphRenderPassCreation        pass_node            = {.Name = name.data(), .Outputs = {{.Name = lighting_output.Name}}};

        pass_node.Inputs.push_back({.Name = "depth_prepass_render_target"});
        pass_node.Inputs.push_back({.Name = "gbuffer_albedo_render_target", .BindingInputKeyName = "AlbedoSampler", .Type = RenderGraphResourceType::TEXTURE});
        pass_node.Inputs.push_back({.Name = "gbuffer_position_render_target", .BindingInputKeyName = "PositionSampler", .Type = RenderGraphResourceType::TEXTURE});
        pass_node.Inputs.push_back({.Name = "gbuffer_normals_render_target", .BindingInputKeyName = "NormalSampler", .Type = RenderGraphResourceType::TEXTURE});
        pass_node.Inputs.push_back({.Name = "gbuffer_specular_render_target", .BindingInputKeyName = "SpecularSampler", .Type = RenderGraphResourceType::TEXTURE});

        builder->CreateRenderPassNode(pass_node);
    }

    void LightingPass::Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph)
    {
        handle = builder.SetPipelineName("Deferred-lighting-Pipeline").EnablePipelineDepthTest(true).UseShader("deferred_lighting").Create();

        auto camera_buffer    = graph.GetBufferUniformSet("scene_camera");
        auto vertex_buffer    = graph.GetBufferSet("g_scene_vertex_buffer");
        auto index_buffer     = graph.GetBufferSet("g_scene_index_buffer");
        auto draw_buffer      = graph.GetBufferSet("g_scene_draw_buffer");
        auto transform_buffer = graph.GetBufferSet("g_scene_transform_buffer");
        auto material_buffer  = graph.GetBufferSet("g_scene_material_buffer");

        handle->SetInput("UBCamera", camera_buffer);
        handle->SetInput("VertexSB", vertex_buffer);
        handle->SetInput("IndexSB", index_buffer);
        handle->SetInput("DrawDataSB", draw_buffer);
        handle->SetInput("TransformSB", transform_buffer);
        handle->SetInput("MatSB", material_buffer);

        auto directional_light_buffer = graph.GetBufferSet("g_scene_directional_light_buffer");
        auto point_light_buffer       = graph.GetBufferSet("g_scene_point_light_buffer");
        auto spot_light_buffer        = graph.GetBufferSet("g_scene_spot_light_buffer");

        handle->SetInput("DirectionalLightSB", directional_light_buffer);
        handle->SetInput("PointLightSB", point_light_buffer);
        handle->SetInput("SpotLightSB", spot_light_buffer);

        handle->Verify();
        handle->Bake();
    }

    void LightingPass::Execute(
        uint32_t                               frame_index,
        Rendering::Scenes::SceneRawData* const scene_data,
        RenderPasses::RenderPass*              pass,
        Buffers::CommandBuffer*                command_buffer,
        RenderGraph* const                     graph)
    {
        auto indirect_buffer          = graph->GetIndirectBufferSet("g_scene_indirect_buffer");
        auto directional_light_buffer = graph->GetBufferSet("g_scene_directional_light_buffer");
        auto point_light_buffer       = graph->GetBufferSet("g_scene_point_light_buffer");
        auto spot_light_buffer        = graph->GetBufferSet("g_scene_spot_light_buffer");
        /*
         * Composing Light Data
         */
        auto dir_light_data   = Lights::CreateLightBuffer<Lights::GpuDirectionLight>(scene_data->DirectionalLights);
        auto point_light_data = Lights::CreateLightBuffer<Lights::GpuPointLight>(scene_data->PointLights);
        auto spot_light_data  = Lights::CreateLightBuffer<Lights::GpuSpotlight>(scene_data->SpotLights);

        directional_light_buffer->SetData<uint8_t>(frame_index, dir_light_data);
        point_light_buffer->SetData<uint8_t>(frame_index, point_light_data);
        spot_light_buffer->SetData<uint8_t>(frame_index, spot_light_data);

        pass->MarkDirty();

        command_buffer->BeginRenderPass(pass);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
