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
        {
            m_env_map = {};
            return false;
        }

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

    bool SkyboxPass::Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        // DrawVertex layout: x y z nx ny nz u v (8 floats = 32 bytes)
        // Normals and UVs zeroed — skybox shader only reads position (location 0, offset 0).
        static constexpr float verts[] = {
            -1.f, -1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, -1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, -1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 1.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f,
        };
        static constexpr uint32_t idxs[] = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4};

        if (!m_geometry_registered)
        {
            auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
            ZENGINE_VALIDATE_ASSERT(rrm, "SkyboxPass::Register: RenderResourceManager not available")
            rrm->RegisterBuiltinGeometry(verts, sizeof(verts), idxs, 36, m_vtx_offset, m_idx_offset);
            m_geometry_registered = true;
        }

        // A missing environment map means the skybox is not part of this frame's
        // graph. ConfigureEnvironmentMap() performs loading outside registration.
        if (!m_env_map.Valid())
            return false;

        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        if (const auto* ticket = rrm ? rrm->FindStreamingUploadTicket(m_env_map) : nullptr)
            res_builder->ReadTexture(res_builder->ImportStreamingTexture("SkyboxEnvironmentMap", *ticket));
        else
            res_builder->ReadTexture(res_builder->ImportTexture("SkyboxEnvironmentMap", m_env_map, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
        return true;
    }

    Specifications::GraphicsPipelineDesc SkyboxPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Skybox-Pipeline";
        desc.EnableDepthTest                      = true;
        desc.EnableDepthWrite                     = false;
        desc.DepthCompareOp                       = 2;
        desc.EnableBlending                       = false;
        desc.CullMode                             = 0;
        desc.ShaderSpecificationValue.Name        = "skybox";

        desc.VertexInputBindingSpecifications.init(arena, 1, 1);
        desc.VertexInputBindingSpecifications[0] = {.Stride = sizeof(float) * 8, .Rate = VK_VERTEX_INPUT_RATE_VERTEX, .Binding = 0};
        desc.VertexInputAttributeSpecifications.init(arena, 1, 1);
        desc.VertexInputAttributeSpecifications[0] = {.Location = 0, .Binding = 0, .Offset = 0, .Format = Specifications::ImageFormat::R32G32B32_SFLOAT};
        return desc;
    }

    void SkyboxPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!scene || !m_env_map.Valid() || !pass)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
        gp->SetTexture("EnvMap", m_env_map);
        gp->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    void SkyboxPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_env_map.Valid())
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer ? framebuffer->Handle : VK_NULL_HANDLE, false);
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool SkyboxPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_env_map.Valid())
            return false;

        auto* gp  = static_cast<RenderPasses::GraphicPass*>(pass);
        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        if (!rrm)
            return false;
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindVertexBuffer(*rrm->GetBuiltinVertexBuffer());
        command_buffer->BindIndexBuffer(*rrm->GetBuiltinIndexBuffer(), VK_INDEX_TYPE_UINT32);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->DrawIndexed(36, 1, m_idx_offset, static_cast<int32_t>(m_vtx_offset), 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
