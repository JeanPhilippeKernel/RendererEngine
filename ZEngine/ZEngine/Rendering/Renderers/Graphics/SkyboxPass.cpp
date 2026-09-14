#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/EnvironmentLighting.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyboxPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void SkyboxPass::SetEnvironment(Textures::TextureHandle environment_map, const Rendering::Scenes::SkyConfig& config)
    {
        m_env_map    = environment_map;
        m_sky_config = config;
    }

    bool SkyboxPass::Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        // A missing map means the environment fallback could not be created. A
        // healthy renderer always sets a valid cubemap before registration.
        if (!m_env_map.Valid())
            return false;

        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        if (const auto* ticket = rrm ? rrm->FindStreamingUploadTicket(m_env_map) : nullptr)
            res_builder->ReadTexture(res_builder->ImportStreamingTexture("SkyboxEnvironmentMap", *ticket));
        else
            res_builder->ReadTexture(res_builder->ImportTexture("SkyboxEnvironmentMap", m_env_map, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->UpdateColorAttachment(RendererResourceName::FrameHdrColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
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
        if (!framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(gp, framebuffer->Handle, false))
            return;
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool SkyboxPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_env_map.Valid())
            return false;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        EnvironmentLightingPushConstants environment = {};
        for (uint32_t index = 0; index < 3; ++index)
            environment.TintIntensity[index] = m_sky_config.EnvironmentTint[index] * m_sky_config.EnvironmentIntensity;
        environment.YawRadians = m_sky_config.EnvironmentYawRadians;
        command_buffer->PushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(environment), &environment);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
