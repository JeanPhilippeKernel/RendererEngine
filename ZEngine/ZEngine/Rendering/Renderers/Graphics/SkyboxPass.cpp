#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyboxPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    bool SkyboxPass::ConfigureEnvironmentMap(cstring path)
    {
        EnvMapPath = path;
        if (!EnvMapPath || EnvMapPath[0] == '\0')
            return false;

        auto* rrm = ZEngine::Engine::GetContext() ? ZEngine::Engine::GetContext()->RenderResourceManager : nullptr;
        if (!rrm)
        {
            ZENGINE_CORE_ERROR("[SkyboxPass] RenderResourceManager not available — cannot load environment map")
            return false;
        }

        auto handle = rrm->SubmitTextureFile(EnvMapPath, m_env_map);
        if (!handle.Valid())
        {
            ZENGINE_CORE_ERROR("[SkyboxPass] Failed to submit environment map '{}'", EnvMapPath)
            return false;
        }

        m_env_map = handle;
        return true;
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
            ConfigureEnvironmentMap(EnvMapPath);

        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
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
            auto* gp = static_cast<RenderPasses::GraphicPass*>(*output_pass);
            gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
            gp->SetTexture("EnvMap", m_env_map);
            gp->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
            gp->Verify();
        }
    }

    void SkyboxPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_env_map.Valid())
            return;

        auto* gp  = static_cast<RenderPasses::GraphicPass*>(pass);
        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        command_buffer->BeginRenderPass(gp, framebuffer->Handle, false);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindVertexBuffer(*rrm->GetBuiltinVertexBuffer());
        command_buffer->BindIndexBuffer(*rrm->GetBuiltinIndexBuffer(), VK_INDEX_TYPE_UINT32);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->DrawIndexed(36, 1, m_idx_offset, static_cast<int32_t>(m_vtx_offset), 0);
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
