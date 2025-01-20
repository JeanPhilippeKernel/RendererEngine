#include <pch.h>
#include <GraphicRenderer.h>
#include <RendererPasses.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    void InitialPass::Setup(std::string_view name, RenderGraph* const graph)
    {
        auto& builder                           = graph->Builder;
        auto& renderer                          = graph->Renderer;

        auto  vb_res                            = builder->CreateBufferSet("initial_vertex_buffer", BufferSetCreationType::VERTEX);
        m_vb_handle                             = vb_res.ResourceInfo.VertexBufferSetHandle;
        RenderGraphRenderPassCreation pass_node = {
            .Name = name.data(), .Outputs = {{.Name = renderer->FrameDepthRenderTargetName.data()}, {.Name = renderer->FrameColorRenderTargetName.data()}}
        };
        builder->CreateRenderPassNode(pass_node);
    }

    void InitialPass::Compile(Ref<RenderPasses::RenderPass>& pass, RenderGraph* const graph, Rendering::Scenes::SceneRawData* const scene)
    {
        auto& builder  = graph->RenderPassBuilder;
        auto& renderer = graph->Renderer;

        if (!pass)
        {
            auto pass_spec = builder->SetPipelineName("Initial-Pipeline").SetInputBindingCount(1).SetStride(0, sizeof(float) * 3).SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX).SetInputAttributeCount(1).SetLocation(0, 0).SetBinding(0, 0).SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT).SetOffset(0, 0).EnablePipelineDepthTest(true).UseShader("initial").Detach();
            pass           = renderer->CreateRenderPass(pass_spec);
            pass->Bake();
        }
    }

    void InitialPass::Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene_data, RenderPasses::RenderPass* const pass, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph)
    {
        auto renderer      = graph->Renderer;
        auto vertex_buffer = renderer->Device->VertexBufferSetManager.Access(m_vb_handle);
        vertex_buffer->SetData<float>(frame_index, m_vertex_data);
    }

    void InitialPass::Render(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBuffer* const command_buffer, RenderGraph* const graph)
    {
        auto renderer      = graph->Renderer;
        auto vertex_buffer = renderer->Device->VertexBufferSetManager.Access(m_vb_handle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindVertexBuffer(vertex_buffer->At(frame_index));
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->Draw(1, 1, 0, 0);
        command_buffer->EndRenderPass();
    }

    void DepthPrePass::Setup(std::string_view name, RenderGraph* const graph)
    {
        auto&                         builder   = graph->Builder;
        auto&                         renderer  = graph->Renderer;
        RenderGraphRenderPassCreation pass_node = {.Name = name.data(), .Inputs = {{.Name = renderer->FrameDepthRenderTargetName.data()}}};
        builder->CreateRenderPassNode(pass_node);
    }

    void DepthPrePass::Compile(Ref<RenderPasses::RenderPass>& pass, RenderGraph* const graph, Rendering::Scenes::SceneRawData* const scene)
    {
        auto& builder  = graph->RenderPassBuilder;
        auto& renderer = graph->Renderer;

        if (!pass)
        {
            auto pass_spec = builder->SetPipelineName("Depth-Prepass-Pipeline").EnablePipelineDepthTest(true).UseShader("depth_prepass_scene").Detach();

            pass           = renderer->CreateRenderPass(pass_spec);
            pass->Bake();
        }

        pass->SetInput("UBCamera", renderer->SceneCameraBufferHandle);

        if (scene)
        {
            pass->SetInput("VertexSB", scene->VertexBufferHandle);
            pass->SetInput("IndexSB", scene->IndexBufferHandle);
            pass->SetInput("DrawDataSB", scene->IndirectDataDrawBufferHandle);
            pass->SetInput("TransformSB", scene->TransformBufferHandle);
        }
        pass->Verify();
    }

    void DepthPrePass::Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* pass, Hardwares::CommandBuffer* command_buffer, RenderGraph* const graph)
    {
        /*
         * Composing Transform Data
         */
        if (!scene || !(scene->TransformBufferHandle))
        {
            return;
        }
        auto& transfor_buffer = graph->Renderer->Device->StorageBufferSetManager.Access(scene->TransformBufferHandle);
        transfor_buffer->SetData<glm::mat4>(frame_index, scene->GlobalTransforms);
    }

    void DepthPrePass::Render(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBuffer* command_buffer, RenderGraph* graph)
    {
        if (!scene || !scene->IndirectBufferHandle)
        {
            return;
        }

        auto  renderer        = graph->Renderer;
        auto& indirect_buffer = renderer->Device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);
        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void SkyboxPass::Setup(std::string_view name, RenderGraph* const graph)
    {
        auto& builder                               = graph->Builder;
        auto& renderer                              = graph->Renderer;
        auto  env_map_res                           = builder->CreateTexture("skybox_env_map", "Settings/EnvironmentMaps/bergen_4k.hdr");

        m_env_map                                   = env_map_res.ResourceInfo.TextureHandle;
        m_vb_handle                                 = renderer->Device->CreateVertexBufferSet();
        m_ib_handle                                 = renderer->Device->CreateIndexBufferSet();

        auto&                         output_skybox = builder->CreateRenderTarget("skybox_render_target", {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM});
        RenderGraphRenderPassCreation pass_node     = {
                .Name = name.data(), .Inputs = {{.Name = renderer->FrameDepthRenderTargetName.data()}, {.Name = renderer->FrameColorRenderTargetName.data()}},
                     .Outputs = {{.Name = output_skybox.Name}}
        };
        builder->CreateRenderPassNode(pass_node);
    }

    void SkyboxPass::Compile(Ref<RenderPasses::RenderPass>& pass, RenderGraph* const graph, Rendering::Scenes::SceneRawData* const scene)
    {
        auto& builder  = graph->RenderPassBuilder;
        auto& renderer = graph->Renderer;

        if (!pass)
        {
            auto pass_spec = builder->SetPipelineName("Skybox-Pipeline").SetInputBindingCount(1).SetStride(0, sizeof(float) * 3).SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX).SetInputAttributeCount(1).SetLocation(0, 0).SetBinding(0, 0).SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT).SetOffset(0, 0).EnablePipelineDepthTest(true).EnablePipelineDepthWrite(false).UseShader("skybox").Detach();
            pass           = renderer->CreateRenderPass(pass_spec);
            pass->Bake();
        }

        auto vertex_buffer = renderer->Device->VertexBufferSetManager.Access(m_vb_handle);
        auto index_buffer  = renderer->Device->IndexBufferSetManager.Access(m_ib_handle);

        auto count         = renderer->Device->SwapchainImageCount;
        for (auto i = 0; i < count; ++i)
        {
            vertex_buffer->SetData<float>(i, m_vertex_data);
            index_buffer->SetData<uint16_t>(i, m_index_data);
        }

        pass->SetInput("UBCamera", renderer->SceneCameraBufferHandle);
        pass->SetInput("EnvMap", m_env_map);
        pass->Verify();
    }

    void SkyboxPass::Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene_data, RenderPasses::RenderPass* pass, Hardwares::CommandBuffer* command_buffer, RenderGraph* const graph) {}

    void SkyboxPass::Render(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBuffer* command_buffer, RenderGraph* graph)
    {
        auto renderer      = graph->Renderer;
        auto vertex_buffer = renderer->Device->VertexBufferSetManager.Access(m_vb_handle);
        auto index_buffer  = renderer->Device->IndexBufferSetManager.Access(m_ib_handle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindVertexBuffer(vertex_buffer->At(frame_index));
        command_buffer->BindIndexBuffer(index_buffer->At(frame_index), VK_INDEX_TYPE_UINT16);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndexed(36, 1, 0, 0, 0);
        command_buffer->EndRenderPass();
    }

    void GridPass::Setup(std::string_view name, RenderGraph* const graph)
    {
        auto& builder                             = graph->Builder;
        auto& renderer                            = graph->Renderer;
        m_vb_handle                               = renderer->Device->CreateVertexBufferSet();
        m_ib_handle                               = renderer->Device->CreateIndexBufferSet();

        auto&                         output_grid = builder->CreateRenderTarget("grid_render_target", {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM});
        RenderGraphRenderPassCreation pass_node   = {
              .Name = name.data(), .Inputs = {{.Name = renderer->FrameDepthRenderTargetName.data()}, {.Name = renderer->FrameColorRenderTargetName.data()}},
                   .Outputs = {{.Name = output_grid.Name}}
        };
        builder->CreateRenderPassNode(pass_node);
    }

    void GridPass::Compile(Ref<RenderPasses::RenderPass>& pass, RenderGraph* const graph, Rendering::Scenes::SceneRawData* const scene)
    {
        auto& builder  = graph->RenderPassBuilder;
        auto& renderer = graph->Renderer;

        if (!pass)
        {
            auto pass_spec = builder->SetPipelineName("Infinite-Grid-Pipeline").SetInputBindingCount(1).SetStride(0, sizeof(float) * 3).SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX).SetInputAttributeCount(1).SetLocation(0, 0).SetBinding(0, 0).SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT).SetOffset(0, 0).EnablePipelineDepthTest(true).UseShader("infinite_grid").Detach();
            pass           = graph->Renderer->CreateRenderPass(pass_spec);
            pass->Bake();
        }

        pass->SetInput("UBCamera", renderer->SceneCameraBufferHandle);
        pass->Verify();

        auto count         = renderer->Device->SwapchainImageCount;
        auto vertex_buffer = renderer->Device->VertexBufferSetManager.Access(m_vb_handle);
        auto index_buffer  = renderer->Device->IndexBufferSetManager.Access(m_ib_handle);
        for (int i = 0; i < count; ++i)
        {
            vertex_buffer->SetData<float>(i, m_vertex_data);
            index_buffer->SetData<uint16_t>(i, m_index_data);
        }
    }

    void GridPass::Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene_data, RenderPasses::RenderPass* pass, Hardwares::CommandBuffer* command_buffer, RenderGraph* const graph) {}

    void GridPass::Render(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBuffer* command_buffer, RenderGraph* graph)
    {
        auto renderer      = graph->Renderer;
        auto vertex_buffer = renderer->Device->VertexBufferSetManager.Access(m_vb_handle);
        auto index_buffer  = renderer->Device->IndexBufferSetManager.Access(m_ib_handle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindVertexBuffer(vertex_buffer->At(frame_index));
        command_buffer->BindIndexBuffer(index_buffer->At(frame_index), VK_INDEX_TYPE_UINT16);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndexed(6, 1, 0, 0, 0);
        command_buffer->EndRenderPass();
    }

    void GbufferPass::Setup(std::string_view name, RenderGraph* const graph)
    {
        auto&                                builder              = graph->Builder;
        auto&                                renderer             = graph->Renderer;

        Specifications::TextureSpecification normal_output_spec   = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification position_output_spec = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification specular_output_spec = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        Specifications::TextureSpecification colour_output_spec   = {.IsUsageStorage = true, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};

        auto&                                gbuffer_albedo       = builder->CreateRenderTarget("gbuffer_albedo_render_target", colour_output_spec);
        auto&                                gbuffer_specular     = builder->CreateRenderTarget("gbuffer_specular_render_target", specular_output_spec);
        auto&                                gbuffer_normals      = builder->CreateRenderTarget("gbuffer_normals_render_target", normal_output_spec);
        auto&                                gbuffer_position     = builder->CreateRenderTarget("gbuffer_position_render_target", position_output_spec);

        RenderGraphRenderPassCreation        pass_node            = {
                              .Name = name.data(), .Inputs = {{.Name = renderer->FrameDepthRenderTargetName.data()}, {.Name = renderer->FrameColorRenderTargetName.data()}}
        };
        pass_node.Outputs.push_back({.Name = gbuffer_albedo.Name});
        pass_node.Outputs.push_back({.Name = gbuffer_specular.Name});
        pass_node.Outputs.push_back({.Name = gbuffer_normals.Name});
        pass_node.Outputs.push_back({.Name = gbuffer_position.Name});
        builder->CreateRenderPassNode(pass_node);
    }

    void GbufferPass::Compile(Ref<RenderPasses::RenderPass>& pass, RenderGraph* const graph, Rendering::Scenes::SceneRawData* const scene)
    {
        auto& builder  = graph->RenderPassBuilder;
        auto& renderer = graph->Renderer;

        if (!pass)
        {
            auto pass_spec = builder->SetPipelineName("GBuffer-Pipeline").EnablePipelineDepthTest(true).UseShader("g_buffer").Detach();
            pass           = renderer->CreateRenderPass(pass_spec);
            pass->Bake();
        }

        pass->SetInput("UBCamera", renderer->SceneCameraBufferHandle);

        if (scene)
        {
            pass->SetInput("VertexSB", scene->VertexBufferHandle);
            pass->SetInput("IndexSB", scene->IndexBufferHandle);
            pass->SetInput("DrawDataSB", scene->IndirectDataDrawBufferHandle);
            pass->SetInput("TransformSB", scene->TransformBufferHandle);
            pass->SetInput("MatSB", scene->MaterialBufferHandle);
        }

        pass->SetBindlessInput("TextureArray");
        pass->Verify();
    }

    void GbufferPass::Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene_data, RenderPasses::RenderPass* pass, Hardwares::CommandBuffer* command_buffer, RenderGraph* const graph) {}

    void GbufferPass::Render(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBuffer* command_buffer, RenderGraph* graph)
    {
        if (!scene || !scene->IndirectBufferHandle)
        {
            return;
        }

        auto renderer        = graph->Renderer;
        auto indirect_buffer = renderer->Device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);
        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }

    void LightingPass::Setup(std::string_view name, RenderGraph* const graph)
    {
        auto&                                builder              = graph->Builder;
        auto&                                renderer             = graph->Renderer;

        Specifications::TextureSpecification lighting_output_spec = {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        auto&                                lighting_output      = builder->CreateRenderTarget("lighting_render_target", lighting_output_spec);
        RenderGraphRenderPassCreation        pass_node            = {.Name = name.data(), .Inputs = {{.Name = renderer->FrameDepthRenderTargetName.data()}}, .Outputs = {{.Name = lighting_output.Name}}};

        pass_node.Inputs.push_back({.Name = "gbuffer_albedo_render_target", .BindingInputKeyName = "AlbedoSampler", .Type = RenderGraphResourceType::TEXTURE});
        pass_node.Inputs.push_back({.Name = "gbuffer_position_render_target", .BindingInputKeyName = "PositionSampler", .Type = RenderGraphResourceType::TEXTURE});
        pass_node.Inputs.push_back({.Name = "gbuffer_normals_render_target", .BindingInputKeyName = "NormalSampler", .Type = RenderGraphResourceType::TEXTURE});
        pass_node.Inputs.push_back({.Name = "gbuffer_specular_render_target", .BindingInputKeyName = "SpecularSampler", .Type = RenderGraphResourceType::TEXTURE});

        builder->CreateRenderPassNode(pass_node);
    }

    void LightingPass::Compile(Ref<RenderPasses::RenderPass>& pass, RenderGraph* const graph, Rendering::Scenes::SceneRawData* const scene)
    {
        auto& builder  = graph->RenderPassBuilder;
        auto& renderer = graph->Renderer;

        if (!pass)
        {
            auto pass_spec = builder->SetPipelineName("Deferred-lighting-Pipeline").EnablePipelineDepthTest(true).UseShader("deferred_lighting").Detach();

            pass           = renderer->CreateRenderPass(pass_spec);
            pass->Bake();
        }

        pass->SetInput("UBCamera", renderer->SceneCameraBufferHandle);
        pass->SetInput("VertexSB", scene->VertexBufferHandle);
        pass->SetInput("IndexSB", scene->IndexBufferHandle);
        pass->SetInput("DrawDataSB", scene->IndirectDataDrawBufferHandle);
        pass->SetInput("TransformSB", scene->TransformBufferHandle);
        pass->SetInput("MatSB", scene->MaterialBufferHandle);

        auto directional_light_buffer = graph->GetStorageBufferSet("g_scene_directional_light_buffer");
        auto point_light_buffer       = graph->GetStorageBufferSet("g_scene_point_light_buffer");
        auto spot_light_buffer        = graph->GetStorageBufferSet("g_scene_spot_light_buffer");

        pass->SetInput("DirectionalLightSB", directional_light_buffer);
        pass->SetInput("PointLightSB", point_light_buffer);
        pass->SetInput("SpotLightSB", spot_light_buffer);

        pass->Verify();
    }

    void LightingPass::Execute(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene_data, RenderPasses::RenderPass* pass, Hardwares::CommandBuffer* command_buffer, RenderGraph* const graph)
    {
        auto directional_light_buffer_handle = graph->GetStorageBufferSet("g_scene_directional_light_buffer");
        auto point_light_buffer_handle       = graph->GetStorageBufferSet("g_scene_point_light_buffer");
        auto spot_light_buffer_handle        = graph->GetStorageBufferSet("g_scene_spot_light_buffer");
        /*
         * Composing Light Data
         */
        auto directional_light_buffer        = graph->Renderer->Device->StorageBufferSetManager.Access(directional_light_buffer_handle);
        auto point_light_buffer              = graph->Renderer->Device->StorageBufferSetManager.Access(point_light_buffer_handle);
        auto spot_light_buffer               = graph->Renderer->Device->StorageBufferSetManager.Access(spot_light_buffer_handle);

        auto dir_light_data                  = Lights::CreateLightBuffer<Lights::GpuDirectionLight>(scene_data->DirectionalLights);
        auto point_light_data                = Lights::CreateLightBuffer<Lights::GpuPointLight>(scene_data->PointLights);
        auto spot_light_data                 = Lights::CreateLightBuffer<Lights::GpuSpotlight>(scene_data->SpotLights);

        directional_light_buffer->SetData<uint8_t>(frame_index, dir_light_data);
        point_light_buffer->SetData<uint8_t>(frame_index, point_light_data);
        spot_light_buffer->SetData<uint8_t>(frame_index, spot_light_data);
    }

    void LightingPass::Render(uint32_t frame_index, Rendering::Scenes::SceneRawData* const scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBuffer* command_buffer, RenderGraph* graph)
    {
        if (!scene->IndirectBufferHandle)
        {
            return;
        }

        auto  renderer        = graph->Renderer;
        auto& indirect_buffer = renderer->Device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);

        command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        command_buffer->BindDescriptorSets(frame_index);
        command_buffer->DrawIndirect(indirect_buffer->At(frame_index));
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers