#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/Contracts/RendererDataContract.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/RendererPasses.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{

    void CompositePass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        res_builder->ReadTexture("gbuffer_albedo_render_target", "sharedRTAsTex");
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {});
    }

    void CompositePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Composite-Pipeline").SetInputBindingCount(0).EnablePipelineDepthTest(false).UseShader("composite").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }
        (*output_pass)->SetSampler("LinearWrapSampler", device->GlobalLinearWrapSamplerImageInfo);
        (*output_pass)->Verify();
    }

    void CompositePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, nullptr, 0u);
        command_buffer->Draw(3, 1, 0, 0);
        command_buffer->EndRenderPass();
    }

    void DepthPrePass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        uint32_t w = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t h = device->SwapchainPtr->SwapchainImageHeight;
        res_builder->WriteDepthAttachment(RendererResourceName::FrameDepthRenderTargetName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE});
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
            *output_pass = device->CreateRenderPass(std::move(pass_spec));
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetDynamicUniform("UBCamera", sizeof(Contracts::UBOCameraLayout));
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
            auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
            if (rrm)
                m_env_map = rrm->SubmitTextureFile(0, 0, EnvMapPath);
        }

        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {});
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
            *output_pass = device->CreateRenderPass(std::move(pass_spec));
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene && m_env_map.Valid())
        {
            (*output_pass)->SetDynamicUniform("UBCamera", sizeof(Contracts::UBOCameraLayout));
            (*output_pass)->SetTexture("EnvMap", m_env_map);
            (*output_pass)->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
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

        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {});
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
            *output_pass = device->CreateRenderPass(std::move(pass_spec));
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetDynamicUniform("UBCamera", sizeof(Contracts::UBOCameraLayout));
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
        uint32_t w = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t h = device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->WriteColorAttachment(RendererResourceName::GBufferAlbedoAOName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferNormalRoughnessName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R16G16B16A16_SFLOAT});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferMetallicEmissiveName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
    }

    void GbufferPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("GBuffer-Pipeline").EnablePipelineDepthTest(true).UseShader("g_buffer").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }

        if (scene)
        {
            (*output_pass)->SetDynamicUniform("UBCamera", sizeof(Contracts::UBOCameraLayout));
            // VertexSB / IndexSB bound by GraphicRenderer::UpdateRMMBindings (RMM path).
            // DrawDataSB/TransformSB/MatSB bound by UpdateRMMBindings via BufferView*.
            (*output_pass)->UseTextureArray("TextureArray");
            (*output_pass)->SetSampler("LinearWrapSampler", device->GlobalLinearWrapSamplerImageInfo);
            (*output_pass)->Verify();
        }
    }

    void GbufferPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        CHECK_AND_ESCAPE_NULL(scene)

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        if (scene->IndirectCommandCount > 0 && scene->RMMVertexHandle.IsValid())
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
            command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
            command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
            command_buffer->DrawIndirect(device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index].Handle, scene->IndirectHeapOffset, scene->IndirectCommandCount);
        }
        command_buffer->EndRenderPass();
    }

    void LightingPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        uint32_t w = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t h = device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadTexture(RendererResourceName::GBufferAlbedoAOName, "GBufferAlbedoAO");
        res_builder->ReadTexture(RendererResourceName::GBufferNormalRoughnessName, "GBufferNormalRoughness");
        res_builder->ReadTexture(RendererResourceName::GBufferMetallicEmissiveName, "GBufferMetallicEmissive");
        res_builder->ReadTexture(RendererResourceName::FrameDepthRenderTargetName, "GBufferDepth");
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM, .LoadOp = LoadOperation::LOAD});
    }

    void LightingPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Deferred-Lighting-Pipeline").SetInputBindingCount(0).EnablePipelineDepthTest(false).UseShader("deferred_lighting").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }

        (*output_pass)->SetDynamicUniform("UBCamera", sizeof(Contracts::UBOCameraLayout));

        auto albedo_ao_handle     = res_inspector->GetRenderTarget(RendererResourceName::GBufferAlbedoAOName);
        auto normal_rough_handle  = res_inspector->GetRenderTarget(RendererResourceName::GBufferNormalRoughnessName);
        auto metallic_emit_handle = res_inspector->GetRenderTarget(RendererResourceName::GBufferMetallicEmissiveName);
        auto depth_handle         = res_inspector->GetRenderTarget(RendererResourceName::FrameDepthRenderTargetName);

        if (albedo_ao_handle.Valid())
            (*output_pass)->SetTexture("GBufferAlbedoAO", albedo_ao_handle);
        if (normal_rough_handle.Valid())
            (*output_pass)->SetTexture("GBufferNormalRoughness", normal_rough_handle);
        if (metallic_emit_handle.Valid())
            (*output_pass)->SetTexture("GBufferMetallicEmissive", metallic_emit_handle);
        if (depth_handle.Valid())
            (*output_pass)->SetTexture("GBufferDepth", depth_handle);

        if (scene && scene->LightBuffer.Handle)
            (*output_pass)->SetStorageBuffer("LightSB", &scene->LightBuffer);

        (*output_pass)->SetSampler("GBufferSampler", device->GlobalLinearWrapSamplerImageInfo);
        (*output_pass)->Verify();
    }

    void LightingPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
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
} // namespace ZEngine::Rendering::Renderers
