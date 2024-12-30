#include <pch.h>
#include <GraphicRenderer.h>
#include <RendererPasses.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    void IndirectRenderingStorage::Initialize(GraphicRenderer* renderer)
    {
        m_vertex_buffer_handle    = renderer->CreateStorageBufferSet();
        m_index_buffer_handle     = renderer->CreateStorageBufferSet();
        m_draw_buffer_handle      = renderer->CreateStorageBufferSet();
        m_transform_buffer_handle = renderer->CreateStorageBufferSet();
        m_indirect_buffer_handle  = renderer->CreateIndirectBufferSet();
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
        auto pass_spec = builder.SetPipelineName("Depth-Prepass-Pipeline").EnablePipelineDepthTest(true).UseShader("depth_prepass_scene").Detach();

        auto camera_buffer    = graph.GetBufferUniformSet("scene_camera");
        auto vertex_buffer    = graph.GetBufferSet("g_scene_vertex_buffer");
        auto index_buffer     = graph.GetBufferSet("g_scene_index_buffer");
        auto draw_buffer      = graph.GetBufferSet("g_scene_draw_buffer");
        auto transform_buffer = graph.GetBufferSet("g_scene_transform_buffer");

        handle = graph.Renderer->CreateRenderPass(pass_spec);
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
        auto vertex_buffer_handle            = graph->GetBufferSet("g_scene_vertex_buffer");
        auto index_buffer_handle             = graph->GetBufferSet("g_scene_index_buffer");
        auto draw_buffer_handle              = graph->GetBufferSet("g_scene_draw_buffer");
        auto transform_buffer_handle         = graph->GetBufferSet("g_scene_transform_buffer");
        auto material_buffer_handle          = graph->GetBufferSet("g_scene_material_buffer");
        auto directional_light_buffer_handle = graph->GetBufferSet("g_scene_directional_light_buffer");
        auto point_light_buffer_handle       = graph->GetBufferSet("g_scene_point_light_buffer");
        auto spot_light_buffer_handle        = graph->GetBufferSet("g_scene_spot_light_buffer");
        auto indirect_buffer_handle          = graph->GetIndirectBufferSet("g_scene_indirect_buffer");

        auto& indirect_buffer = graph->Renderer->IndirectBufferSetManager.Access(indirect_buffer_handle);
        /*
         * Composing Transform Data
         */
        auto& transfor_buffer = graph->Renderer->StorageBufferSetManager.Access(transform_buffer_handle);
        transfor_buffer->SetData<glm::mat4>(frame_index, scene_data->GlobalTransformCollection);

        /*
         * Scene Draw data
         */
        bool perform_draw_update = true;
        if ((m_cached_vertices_count[frame_index] == scene_data->Vertices.size()) || (m_cached_indices_count[frame_index] == scene_data->Indices.size()))
        {
            perform_draw_update = false;
        }

        if (!perform_draw_update)
        {
            return;
        }

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
        auto& vertex_buffer = graph->Renderer->StorageBufferSetManager.Access(vertex_buffer_handle);
        auto& index_buffer  = graph->Renderer->StorageBufferSetManager.Access(index_buffer_handle);
        vertex_buffer->SetData<float>(frame_index, scene_data->Vertices);
        index_buffer->SetData<uint32_t>(frame_index, scene_data->Indices);
        /*
         * Uploading Drawing data
         */
        auto& draw_buffer = graph->Renderer->StorageBufferSetManager.Access(draw_buffer_handle);
        draw_buffer->SetData<DrawData>(frame_index, draw_data_collection);
        /*
         * Uploading Material data
         */
        auto& material_buffer = graph->Renderer->StorageBufferSetManager.Access(material_buffer_handle);
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

        indirect_buffer->SetData<VkDrawIndirectCommand>(frame_index, draw_indirect_commmand);

        /*
         * Caching last vertex/index count per frame
         */
        m_cached_vertices_count[frame_index] = scene_data->Vertices.size();
        m_cached_indices_count[frame_index]  = scene_data->Indices.size();

        /*
         * Mark RenderPass dirty and should re-update inputs
         */
        pass->MarkDirty();
    }

    void SceneDepthPrePass::Render(
        uint32_t                   frame_index,
        RenderPasses::RenderPass*  pass,
        Buffers::FramebufferVNext* framebuffer,
        Buffers::CommandBuffer*    command_buffer,
        RenderGraph*               graph)
    {
        auto renderer = graph->Renderer;

        pass->Update(frame_index);
        renderer->WriteDescriptorSets(pass->EnqueuedWriteDescriptorSetRequests);

        auto  indirect_buffer_handle = graph->GetIndirectBufferSet("g_scene_indirect_buffer");
        auto& indirect_buffer        = renderer->IndirectBufferSetManager.Access(indirect_buffer_handle);
        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();

        ZENGINE_CLEAR_STD_VECTOR(pass->EnqueuedWriteDescriptorSetRequests)
    }

    void SkyboxPass::Setup(std::string_view name, RenderGraphBuilder* const builder)
    {
        builder->CreateTexture("skybox_env_map", "Settings/EnvironmentMaps/bergen_4k.hdr");
        RenderGraphRenderPassCreation pass_node = {.Name = name.data(), .Inputs = {{.Name = "depth_prepass_render_target"}, {.Name = "lighting_render_target"}}};
        builder->CreateRenderPassNode(pass_node);
    }

    void SkyboxPass::Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph)
    {
        auto pass_spec      = builder.SetPipelineName("Skybox-Pipeline").EnablePipelineDepthTest(true).EnablePipelineDepthWrite(false).UseShader("skybox").Detach();
        auto camera_buffer  = graph.GetBufferUniformSet("scene_camera");
        auto skybox_env_map = graph.GetTexture("skybox_env_map");

        handle = graph.Renderer->CreateRenderPass(pass_spec);
        handle->SetInput("UBCamera", camera_buffer);
        handle->SetInput("VertexSB", m_vertex_buffer_handle);
        handle->SetInput("IndexSB", m_index_buffer_handle);
        handle->SetInput("DrawDataSB", m_draw_buffer_handle);
        handle->SetInput("TransformSB", m_transform_buffer_handle);
        handle->SetInput("CubemapTexture", skybox_env_map);
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
        auto renderer = graph->Renderer;

        auto vertex_buffer    = renderer->StorageBufferSetManager.Access(m_vertex_buffer_handle);
        auto index_buffer     = renderer->StorageBufferSetManager.Access(m_index_buffer_handle);
        auto draw_buffer      = renderer->StorageBufferSetManager.Access(m_draw_buffer_handle);
        auto transform_buffer = renderer->StorageBufferSetManager.Access(m_transform_buffer_handle);
        auto indirect_buffer  = renderer->IndirectBufferSetManager.Access(m_indirect_buffer_handle);

        vertex_buffer->SetData<float>(frame_index, m_vertex_data);
        index_buffer->SetData<uint32_t>(frame_index, m_index_data);
        draw_buffer->SetData<DrawData>(frame_index, m_draw_data);
        transform_buffer->SetData<glm::mat4>(frame_index, std::vector<glm::mat4>{glm::identity<glm::mat4>()});
        indirect_buffer->SetData<VkDrawIndirectCommand>(frame_index, m_indirect_commmand);

        pass->MarkDirty();
    }

    void SkyboxPass::Render(
        uint32_t                   frame_index,
        RenderPasses::RenderPass*  pass,
        Buffers::FramebufferVNext* framebuffer,
        Buffers::CommandBuffer*    command_buffer,
        RenderGraph*               graph)
    {
        auto renderer = graph->Renderer;

        pass->Update(frame_index);
        renderer->WriteDescriptorSets(pass->EnqueuedWriteDescriptorSetRequests);
        auto indirect_buffer = renderer->IndirectBufferSetManager.Access(m_indirect_buffer_handle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();

        ZENGINE_CLEAR_STD_VECTOR(pass->EnqueuedWriteDescriptorSetRequests)
    }

    void GridPass::Setup(std::string_view name, RenderGraphBuilder* const builder)
    {
        RenderGraphRenderPassCreation pass_node = {.Name = name.data(), .Inputs = {{.Name = "depth_prepass_render_target"}, {.Name = "lighting_render_target"}}};
        builder->CreateRenderPassNode(pass_node);
    }

    void GridPass::Compile(Ref<RenderPasses::RenderPass>& handle, RenderPasses::RenderPassBuilder& builder, RenderGraph& graph)
    {
        auto pass_spec = builder.SetPipelineName("Infinite-Grid-Pipeline").EnablePipelineDepthTest(true).UseShader("infinite_grid").Detach();

        auto camera_buffer = graph.GetBufferUniformSet("scene_camera");

        handle = graph.Renderer->CreateRenderPass(pass_spec);
        handle->SetInput("UBCamera", camera_buffer);
        handle->SetInput("VertexSB", m_vertex_buffer_handle);
        handle->SetInput("IndexSB", m_index_buffer_handle);
        handle->SetInput("DrawDataSB", m_draw_buffer_handle);
        handle->SetInput("TransformSB", m_transform_buffer_handle);
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
            auto vertex_buffer    = graph->Renderer->StorageBufferSetManager.Access(m_vertex_buffer_handle);
            auto index_buffer     = graph->Renderer->StorageBufferSetManager.Access(m_index_buffer_handle);
            auto draw_buffer      = graph->Renderer->StorageBufferSetManager.Access(m_draw_buffer_handle);
            auto transform_buffer = graph->Renderer->StorageBufferSetManager.Access(m_transform_buffer_handle);
            auto indirect_buffer  = graph->Renderer->IndirectBufferSetManager.Access(m_indirect_buffer_handle);

            vertex_buffer->SetData<float>(frame_index, m_vertex_data);
            index_buffer->SetData<uint32_t>(frame_index, m_index_data);
            draw_buffer->SetData<DrawData>(frame_index, m_draw_data);
            transform_buffer->SetData<glm::mat4>(frame_index, std::vector<glm::mat4>{glm::identity<glm::mat4>()});
            indirect_buffer->SetData<VkDrawIndirectCommand>(frame_index, m_indirect_commmand);

            pass->MarkDirty();
        })
    }

    void GridPass::Render(uint32_t frame_index, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Buffers::CommandBuffer* command_buffer, RenderGraph* graph)
    {
        auto renderer = graph->Renderer;
        pass->Update(frame_index);
        renderer->WriteDescriptorSets(pass->EnqueuedWriteDescriptorSetRequests);

        auto indirect_buffer = renderer->IndirectBufferSetManager.Access(m_indirect_buffer_handle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();

        ZENGINE_CLEAR_STD_VECTOR(pass->EnqueuedWriteDescriptorSetRequests)
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
        auto pass_spec = builder.SetPipelineName("GBuffer-Pipeline").EnablePipelineDepthTest(true).UseShader("g_buffer").Detach();
        handle         = graph.Renderer->CreateRenderPass(pass_spec);

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
        if ((m_cached_vertices_count[frame_index] != scene_data->Vertices.size()) || (m_cached_indices_count[frame_index] != scene_data->Indices.size()))
        {
            pass->MarkDirty();
            m_cached_vertices_count[frame_index] = scene_data->Vertices.size();
            m_cached_indices_count[frame_index]  = scene_data->Indices.size();
        }

        // GraphicRenderer::BindGlobalTextures(pass);
    }

    void GbufferPass::Render(
        uint32_t                   frame_index,
        RenderPasses::RenderPass*  pass,
        Buffers::FramebufferVNext* framebuffer,
        Buffers::CommandBuffer*    command_buffer,
        RenderGraph*               graph)
    {
        auto renderer = graph->Renderer;
        pass->Update(frame_index);
        renderer->WriteDescriptorSets(pass->EnqueuedWriteDescriptorSetRequests);

        auto indirect_buffer_handle = graph->GetIndirectBufferSet("g_scene_indirect_buffer");
        auto indirect_buffer        = renderer->IndirectBufferSetManager.Access(indirect_buffer_handle);
        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();

        ZENGINE_CLEAR_STD_VECTOR(pass->EnqueuedWriteDescriptorSetRequests)
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
        auto pass_spec        = builder.SetPipelineName("Deferred-lighting-Pipeline").EnablePipelineDepthTest(true).UseShader("deferred_lighting").Detach();
        auto camera_buffer    = graph.GetBufferUniformSet("scene_camera");
        auto vertex_buffer    = graph.GetBufferSet("g_scene_vertex_buffer");
        auto index_buffer     = graph.GetBufferSet("g_scene_index_buffer");
        auto draw_buffer      = graph.GetBufferSet("g_scene_draw_buffer");
        auto transform_buffer = graph.GetBufferSet("g_scene_transform_buffer");
        auto material_buffer  = graph.GetBufferSet("g_scene_material_buffer");

        handle = graph.Renderer->CreateRenderPass(pass_spec);
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
        auto directional_light_buffer_handle = graph->GetBufferSet("g_scene_directional_light_buffer");
        auto point_light_buffer_handle       = graph->GetBufferSet("g_scene_point_light_buffer");
        auto spot_light_buffer_handle        = graph->GetBufferSet("g_scene_spot_light_buffer");
        /*
         * Composing Light Data
         */
        auto directional_light_buffer = graph->Renderer->StorageBufferSetManager.Access(directional_light_buffer_handle);
        auto point_light_buffer       = graph->Renderer->StorageBufferSetManager.Access(point_light_buffer_handle);
        auto spot_light_buffer        = graph->Renderer->StorageBufferSetManager.Access(spot_light_buffer_handle);

        auto dir_light_data   = Lights::CreateLightBuffer<Lights::GpuDirectionLight>(scene_data->DirectionalLights);
        auto point_light_data = Lights::CreateLightBuffer<Lights::GpuPointLight>(scene_data->PointLights);
        auto spot_light_data  = Lights::CreateLightBuffer<Lights::GpuSpotlight>(scene_data->SpotLights);

        directional_light_buffer->SetData<uint8_t>(frame_index, dir_light_data);
        point_light_buffer->SetData<uint8_t>(frame_index, point_light_data);
        spot_light_buffer->SetData<uint8_t>(frame_index, spot_light_data);

        pass->MarkDirty();
    }

    void LightingPass::Render(
        uint32_t                   frame_index,
        RenderPasses::RenderPass*  pass,
        Buffers::FramebufferVNext* framebuffer,
        Buffers::CommandBuffer*    command_buffer,
        RenderGraph*               graph)
    {
        auto renderer = graph->Renderer;
        pass->Update(frame_index);
        renderer->WriteDescriptorSets(pass->EnqueuedWriteDescriptorSetRequests);

        auto indirect_buffer_handle = graph->GetIndirectBufferSet("g_scene_indirect_buffer");
        auto indirect_buffer        = renderer->IndirectBufferSetManager.Access(indirect_buffer_handle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();

        ZENGINE_CLEAR_STD_VECTOR(pass->EnqueuedWriteDescriptorSetRequests)
    }
} // namespace ZEngine::Rendering::Renderers