#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/EnvironmentBackgroundPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        constexpr float kSolidFallbackColor[3] = {0.08f, 0.22f, 0.48f};
    }

    void EnvironmentBackgroundPass::SetEnvironment(Textures::TextureHandle environment_map, const Rendering::Scenes::SkyConfig& config)
    {
        m_environment_map     = environment_map;
        m_presentation_config = config;
    }

    void EnvironmentBackgroundPass::SetActive(bool active)
    {
        m_active = active;
    }

    void EnvironmentBackgroundPass::SetUseSolidColorFallback(bool enabled)
    {
        m_use_solid_color_fallback = enabled;
    }

    void EnvironmentBackgroundPass::SetCameraDepthConvention(bool uses_reverse_z)
    {
        m_uses_reverse_z = uses_reverse_z;
    }

    bool EnvironmentBackgroundPass::IsActive() const
    {
        return m_active;
    }

    bool EnvironmentBackgroundPass::Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        // A missing map means the environment fallback could not be created. A
        // healthy renderer always sets a valid cubemap before registration.
        if (!res_builder || !IsActive() || !m_environment_map.Valid())
            return false;

        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        if (const auto* ticket = rrm ? rrm->FindStreamingUploadTicket(m_environment_map) : nullptr)
            res_builder->ReadTexture(res_builder->ImportStreamingTexture("EnvironmentBackgroundMap", *ticket));
        else
            res_builder->ReadTexture(res_builder->ImportTexture("EnvironmentBackgroundMap", m_environment_map, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->UpdateColorAttachment(RendererResourceName::FrameHdrColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
        return true;
    }

    Specifications::GraphicsPipelineDesc EnvironmentBackgroundPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Environment-Background-Pipeline";
        desc.EnableDepthTest                      = true;
        desc.EnableDepthWrite                     = false;
        desc.DepthCompareOp                       = VK_COMPARE_OP_EQUAL;
        desc.EnableBlending                       = false;
        desc.CullMode                             = 0;
        desc.ShaderSpecificationValue.Name        = "environment_background";

        return desc;
    }

    void EnvironmentBackgroundPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!IsActive() || !device || !scene || !m_environment_map.Valid() || !pass)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
        gp->SetTexture("EnvMap", m_environment_map);
        gp->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    void EnvironmentBackgroundPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive() || !device || !scene || !pass || !command_buffer || !m_environment_map.Valid() || !framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(gp, framebuffer->Handle, false))
            return;
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool EnvironmentBackgroundPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive() || !device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame || !scene || !pass || !command_buffer || !m_environment_map.Valid())
            return false;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
        EnvironmentBackgroundPushConstants environment = {};
        if (m_use_solid_color_fallback)
        {
            for (uint32_t index = 0; index < 3; ++index)
                environment.TintIntensity[index] = kSolidFallbackColor[index];
            environment.UseSolidColorFallback = 1.0f;
        }
        else
        {
            for (uint32_t index = 0; index < 3; ++index)
                environment.TintIntensity[index] = m_presentation_config.EnvironmentTint[index] * m_presentation_config.EnvironmentIntensity;
            environment.YawRadians = m_presentation_config.EnvironmentYawRadians;
        }
        environment.FarDepth = m_uses_reverse_z ? 0.0f : 1.0f;
        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(environment), &environment);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
