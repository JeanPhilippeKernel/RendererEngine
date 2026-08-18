#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/Contracts/RendererDataContract.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/RendererPasses.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{

    void BasePass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        RenderGraphRenderPassCreation pass_node = {.Name = name};

        pass_node.Inputs.init(device->Arena, 1);
        pass_node.Outputs.init(device->Arena, 1);

        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});

        res_builder->CreateRenderPassNode(pass_node);
    }

    void BasePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Base-Pipeline")
                                 .SetInputBindingCount(0)
                                 .EnablePipelineDepthTest(true)

                                 .UseShader("base")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }
        (*output_pass)->Verify();
    }

    void BasePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->Draw(3, 1, 0, 0);
        command_buffer->EndRenderPass();
    }

    void DepthPrePass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        RenderGraphRenderPassCreation pass_node = {.Name = name};

        pass_node.Inputs.init(device->Arena, 1);
        pass_node.Outputs.init(device->Arena, 1);
        pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});

        res_builder->CreateRenderPassNode(pass_node);
    }

    void DepthPrePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Depth-Prepass-Pipeline")
                                 .EnablePipelineDepthTest(true)

                                 .UseShader("depth_prepass_scene")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetInputFromHeap("UBCamera", sizeof(Contracts::UBOCameraLayout));
            // VertexSB/IndexSB/DrawDataSB/TransformSB bound by UpdateRMMBindings via BufferView*.
            (*output_pass)->Verify();
        }
    }

    void DepthPrePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene || scene->IndirectCommandCount == 0 || !scene->RMMVertexHandle.IsValid())
        {
            return;
        }

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->DrawIndirect(device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index].Handle, scene->IndirectHeapOffset, scene->IndirectCommandCount);
        command_buffer->EndRenderPass();
    }

    void SkyboxPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        // DrawVertex layout: x y z nx ny nz u v (8 floats = 32 bytes)
        // Normals and UVs zeroed — skybox shader only reads position (location 0, offset 0).
        static constexpr float verts[] = {
            -1.f, -1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, -1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, -1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f,
        };
        static constexpr uint32_t idxs[] = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4};

        auto*                     rrm    = ZEngine::Engine::GetContext()->RenderResourceManager;
        ZENGINE_VALIDATE_ASSERT(rrm, "SkyboxPass::Setup: RenderResourceManager not available")
        rrm->RegisterBuiltinGeometry(verts, sizeof(verts), idxs, 36, m_vtx_offset, m_idx_offset);

        bool env_map_available = false;
        if (EnvMapPath && EnvMapPath[0] != '\0')
        {
            auto* vfs = ZEngine::Engine::GetContext() ? ZEngine::Engine::GetContext()->VFS : nullptr;
            if (vfs)
            {
                auto path_result   = ZEngine::Core::VFS::VFSPath::FromNative(EnvMapPath);
                auto exists_result = path_result.Succeeded() ? vfs->Exists(path_result.Value()) : ZEngine::Core::VFS::VFSResult<bool>::Fail(ZEngine::Core::VFS::VFSError::InvalidPath);
                if (exists_result.Failed() || !exists_result.Value())
                    ZENGINE_CORE_ERROR("[SkyboxPass] Environment map not found in VFS: {}", EnvMapPath)
                else
                    env_map_available = true;
            }
            else
            {
                ZENGINE_CORE_ERROR("[SkyboxPass] VFS not available — cannot resolve environment map path: {}", EnvMapPath)
            }
        }

        if (env_map_available)
        {
            auto env_map_res = res_builder->CreateTexture("skybox_env_map", EnvMapPath);
            m_env_map        = env_map_res.ResourceInfo.TextureHandle;
        }

        RenderGraphRenderPassCreation pass_node = {.Name = name};
        pass_node.Inputs.init(device->Arena, 2);
        pass_node.Outputs.init(device->Arena, 1);
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});
        res_builder->CreateRenderPassNode(pass_node);
    }

    void SkyboxPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Skybox-Pipeline")
                                 .SetInputBindingCount(1)
                                 .SetStride(0, sizeof(float) * 8)
                                 .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)
                                 .SetInputAttributeCount(1)
                                 .SetLocation(0, 0)

                                 .SetBinding(0, 0)
                                 .SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT)
                                 .SetOffset(0, 0)
                                 .EnablePipelineDepthTest(true)
                                 .EnablePipelineDepthWrite(false)
                                 .PipelineDepthCompareOp(2)

                                 .EnablePipelineBlending(false)

                                 .SetCullMode(0)
                                 .UseShader("skybox")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene && m_env_map.Valid())
        {
            (*output_pass)->SetInputFromHeap("UBCamera", sizeof(Contracts::UBOCameraLayout));
            (*output_pass)->SetInput("EnvMap", m_env_map);
            (*output_pass)->SetInput("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
            (*output_pass)->Verify();
        }
    }

    void SkyboxPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_env_map.Valid())
            return;

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindVertexBuffer(*rrm->GetGlobalVertexBuffer());
        command_buffer->BindIndexBuffer(*rrm->GetGlobalIndexBuffer(), VK_INDEX_TYPE_UINT32);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->DrawIndexed(36, 1, m_idx_offset, static_cast<int32_t>(m_vtx_offset), 0);
        command_buffer->EndRenderPass();
    }

    void GridPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        // Y=0 — the vertex shader adds groundY + 0.001 epsilon for z-fighting prevention.
        // DrawVertex layout: x y z nx ny nz u v (8 floats = 32 bytes)
        // Normal points up (0,1,0); UVs mapped to world XZ position.
        static constexpr float verts[] = {
            -1000.f, 0.f, -1000.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1000.f, 0.f, -1000.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1000.f, 0.f, 1000.f, 0.f, 1.f, 0.f, 1.f, 1.f, -1000.f, 0.f, 1000.f, 0.f, 1.f, 0.f, 0.f, 1.f,
        };
        static constexpr uint32_t idxs[] = {0, 1, 2, 2, 3, 0};

        auto*                     rrm    = ZEngine::Engine::GetContext()->RenderResourceManager;
        ZENGINE_VALIDATE_ASSERT(rrm, "GridPass::Setup: RenderResourceManager not available")
        rrm->RegisterBuiltinGeometry(verts, sizeof(verts), idxs, 6, m_vtx_offset, m_idx_offset);

        RenderGraphRenderPassCreation pass_node = {.Name = name};
        pass_node.Inputs.init(device->Arena, 2);
        pass_node.Outputs.init(device->Arena, 1);
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});
        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameColorRenderTargetName});
        res_builder->CreateRenderPassNode(pass_node);
    }

    void GridPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Infinite-Grid-Pipeline")
                                 .SetInputBindingCount(1)
                                 .SetStride(0, sizeof(float) * 8)
                                 .SetRate(0, VK_VERTEX_INPUT_RATE_VERTEX)
                                 .SetInputAttributeCount(1)
                                 .SetLocation(0, 0)

                                 .SetBinding(0, 0)
                                 .SetFormat(0, Specifications::ImageFormat::R32G32B32_SFLOAT)
                                 .SetOffset(0, 0)

                                 .EnablePipelineDepthTest(true)
                                 .EnablePipelineDepthWrite(false)
                                 .PipelineDepthCompareOp(3)

                                 .EnablePipelineBlending(true)

                                 .SetCullMode(0)
                                 .UseShader("infinite_grid")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(pass_spec);
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetInputFromHeap("UBCamera", sizeof(Contracts::UBOCameraLayout));
        }
        (*output_pass)->Verify();
    }

    void GridPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindVertexBuffer(*rrm->GetGlobalVertexBuffer());
        command_buffer->BindIndexBuffer(*rrm->GetGlobalIndexBuffer(), VK_INDEX_TYPE_UINT32);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GridPushConstantData), &PushData);
        command_buffer->DrawIndexed(6, 1, m_idx_offset, static_cast<int32_t>(m_vtx_offset), 0);
        command_buffer->EndRenderPass();
    }

    void GbufferPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        uint32_t                             rt_w                 = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t                             rt_h                 = device->SwapchainPtr->SwapchainImageHeight;
        Specifications::TextureSpecification normal_output_spec   = {.IsUsageStorage = true, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification position_output_spec = {.IsUsageStorage = true, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::R16G16B16A16_SFLOAT};
        Specifications::TextureSpecification specular_output_spec = {.IsUsageStorage = true, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::R8G8B8A8_UNORM};
        Specifications::TextureSpecification colour_output_spec   = {.IsUsageStorage = true, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::R8G8B8A8_UNORM};

        auto&                                gbuffer_albedo       = res_builder->CreateRenderTarget("gbuffer_albedo_render_target", colour_output_spec);
        auto&                                gbuffer_specular     = res_builder->CreateRenderTarget("gbuffer_specular_render_target", specular_output_spec);
        auto&                                gbuffer_normals      = res_builder->CreateRenderTarget("gbuffer_normals_render_target", normal_output_spec);
        auto&                                gbuffer_position     = res_builder->CreateRenderTarget("gbuffer_position_render_target", position_output_spec);

        RenderGraphRenderPassCreation        pass_node            = {.Name = name};

        pass_node.Inputs.init(device->Arena, 2);
        pass_node.Outputs.init(device->Arena, 4);

        pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = RendererResourceName::FrameDepthRenderTargetName});

        pass_node.Outputs.push({.Name = gbuffer_albedo.Name});
        pass_node.Outputs.push({.Name = gbuffer_specular.Name});
        pass_node.Outputs.push({.Name = gbuffer_normals.Name});
        pass_node.Outputs.push({.Name = gbuffer_position.Name});
        res_builder->CreateRenderPassNode(pass_node);
    }

    void GbufferPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("GBuffer-Pipeline").EnablePipelineDepthTest(true).UseShader("g_buffer").Detach();
            *output_pass   = device->CreateRenderPass(pass_spec);
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetInputFromHeap("UBCamera", sizeof(Contracts::UBOCameraLayout));
            // VertexSB / IndexSB bound by GraphicRenderer::UpdateRMMBindings (RMM path).
            // DrawDataSB/TransformSB/MatSB bound by UpdateRMMBindings via BufferView*.
            (*output_pass)->SetBindlessInput("TextureArray");
            (*output_pass)->Verify();
        }
    }

    void GbufferPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        CHECK_AND_ESCAPE_NULL(scene)
        if (scene->IndirectCommandCount == 0 || !scene->RMMVertexHandle.IsValid())
            return;

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->DrawIndirect(device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index].Handle, scene->IndirectHeapOffset, scene->IndirectCommandCount);
        command_buffer->EndRenderPass();
    }

    void LightingPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        // auto&                                builder              = graph->Builder;
        // auto&                                renderer             = graph->Renderer;

        // Specifications::TextureSpecification lighting_output_spec = {.Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM};
        // auto&                                lighting_output      = builder->CreateRenderTarget("lighting_render_target", lighting_output_spec);
        // RenderGraphRenderPassCreation        pass_node            = {.Name = name.data()};

        // pass_node.Inputs.init(graph->Renderer->Device->Arena, 5);
        // pass_node.Outputs.init(graph->Renderer->Device->Arena, 1);

        // pass_node.Inputs.push(RenderGraphRenderPassInputOutputInfo{.Name = renderer->FrameDepthRenderTargetName});
        // pass_node.Inputs.push({.Name = "gbuffer_albedo_render_target", .BindingInputKeyName = "AlbedoSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Inputs.push({.Name = "gbuffer_position_render_target", .BindingInputKeyName = "PositionSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Inputs.push({.Name = "gbuffer_normals_render_target", .BindingInputKeyName = "NormalSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Inputs.push({.Name = "gbuffer_specular_render_target", .BindingInputKeyName = "SpecularSampler", .Type = RenderGraphResourceType::TEXTURE});
        // pass_node.Outputs.push(RenderGraphRenderPassInputOutputInfo{.Name = lighting_output.Name});

        // builder->CreateRenderPassNode(pass_node);
    }

    void LightingPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        // if (!pass)
        //{
        //     return;
        // }

        // auto& builder  = graph->RenderPassBuilder;
        // auto& renderer = graph->Renderer;

        // if (pass && !(*pass))
        //{
        //     auto pass_spec = builder->SetPipelineName("Deferred-lighting-Pipeline").EnablePipelineDepthTest(true).UseShader("deferred_lighting").Detach();

        //    *pass          = renderer->CreateRenderPass(pass_spec);
        //    (*pass)->Bake();
        //}

        //(*pass)->SetInput("UBCamera", renderer->SceneCameraBufferHandle);
        //(*pass)->SetInput("VertexSB", scene->VertexBufferHandle);
        //(*pass)->SetInput("IndexSB", scene->IndexBufferHandle);
        //(*pass)->SetInput("DrawDataSB", scene->IndirectDataDrawBufferHandle);
        //(*pass)->SetInput("TransformSB", scene->TransformBufferHandle);
        //(*pass)->SetInput("MatSB", scene->MaterialBufferHandle);

        // auto directional_light_buffer = graph->GetStorageBufferSet("g_scene_directional_light_buffer");
        // auto point_light_buffer       = graph->GetStorageBufferSet("g_scene_point_light_buffer");
        // auto spot_light_buffer        = graph->GetStorageBufferSet("g_scene_spot_light_buffer");

        //(*pass)->SetInput("DirectionalLightSB", directional_light_buffer);
        //(*pass)->SetInput("PointLightSB", point_light_buffer);
        //(*pass)->SetInput("SpotLightSB", spot_light_buffer);

        //(*pass)->Verify();
    }

    void LightingPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        // auto directional_light_buffer_handle = graph->GetStorageBufferSet("g_scene_directional_light_buffer");
        // auto point_light_buffer_handle       = graph->GetStorageBufferSet("g_scene_point_light_buffer");
        // auto spot_light_buffer_handle        = graph->GetStorageBufferSet("g_scene_spot_light_buffer");
        ///*
        // * Composing Light Data
        // */
        // auto directional_light_buffer        = graph->Renderer->Device->StorageBufferSetManager.Access(directional_light_buffer_handle);
        // auto point_light_buffer              = graph->Renderer->Device->StorageBufferSetManager.Access(point_light_buffer_handle);
        // auto spot_light_buffer               = graph->Renderer->Device->StorageBufferSetManager.Access(spot_light_buffer_handle);

        // auto dir_light_data                  = Lights::CreateLightBuffer<Lights::GpuDirectionLight>(scene_data->DirectionalLights);
        // auto point_light_data                = Lights::CreateLightBuffer<Lights::GpuPointLight>(scene_data->PointLights);
        // auto spot_light_data                 = Lights::CreateLightBuffer<Lights::GpuSpotlight>(scene_data->SpotLights);

        // directional_light_buffer->SetData<uint8_t>(frame_index, dir_light_data);
        // point_light_buffer->SetData<uint8_t>(frame_index, point_light_data);
        // spot_light_buffer->SetData<uint8_t>(frame_index, spot_light_data);

        // if (!scene->IndirectBufferHandle)
        //{
        //     return;
        // }

        // auto renderer        = graph->Renderer;
        // auto indirect_buffer = renderer->Device->IndirectBufferSetManager.Access(scene->IndirectBufferHandle);

        // command_buffer->BeginRenderPass(pass, framebuffer->Handle);
        // command_buffer->BindDescriptorSets(scene->FrameIndex);
        // command_buffer->DrawIndirect(*indirect_buffer->At(scene->FrameIndex));
        // command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
