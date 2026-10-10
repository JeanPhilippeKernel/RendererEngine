#include <ZEngine/Rendering/Renderers/Graphics/SkySpherePass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    void SkySpherePass::SetEnvironment(const Scenes::SkyConfig& config, const Scenes::SkyCelestialLight& celestial_light)
    {
        m_active = config.IsSkySphere() && config.IsValid();
        if (!m_active)
        {
            m_config    = {};
            m_celestial = {};
            return;
        }

        m_config    = config;
        m_celestial = celestial_light.IsValid() ? celestial_light : Scenes::SkyCelestialLight{};
    }

    void SkySpherePass::SetCameraDepthConvention(bool uses_reverse_z)
    {
        m_uses_reverse_z = uses_reverse_z;
    }

    bool SkySpherePass::IsViewActive() const
    {
        return m_active;
    }

    bool SkySpherePass::Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        if (!res_builder || !IsViewActive())
            return false;

        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->UpdateColorAttachment(RendererResourceName::FrameHdrColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
        return true;
    }

    GraphicsPipelineDesc SkySpherePass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        GraphicsPipelineDesc description          = {};
        description.DebugName                     = "Sky-Sphere-Pipeline";
        description.EnableDepthTest               = true;
        description.EnableDepthWrite              = false;
        description.DepthCompareOp                = VK_COMPARE_OP_EQUAL;
        description.EnableBlending                = false;
        description.CullMode                      = VK_CULL_MODE_NONE;
        description.ShaderSpecificationValue.Name = "sky_sphere";
        return description;
    }

    void SkySpherePass::Prepare(Hardwares::VulkanDevicePtr const /*device*/, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr const /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!scene || !pass || !IsViewActive())
            return;

        static_cast<RenderPasses::GraphicPass*>(pass)->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
    }

    void SkySpherePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsViewActive() || !device || !scene || !pass || !command_buffer || !framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;

        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(graphic_pass, framebuffer->Handle, false))
            return;
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool SkySpherePass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsViewActive() || !device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame || !scene || !pass || !command_buffer)
            return false;

        auto* const            graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        SkySpherePushConstants push         = {};
        for (uint32_t index = 0; index < 3; ++index)
        {
            const float tint         = m_config.EnvironmentTint[index] * m_config.EnvironmentIntensity;
            push.HorizonColor[index] = m_config.Sphere.HorizonColor[index] * tint;
            push.ZenithColor[index]  = m_config.Sphere.ZenithColor[index] * tint;
            push.GroundColor[index]  = m_config.Sphere.GroundColor[index] * tint;
            push.SunDirection[index] = m_celestial.DirectionToLight[index];
        }
        push.HorizonColor[3]             = 1.0f;
        push.ZenithColor[3]              = 1.0f;
        push.GroundColor[3]              = 1.0f;
        push.SunDirection[3]             = m_uses_reverse_z ? 0.0f : 1.0f;
        push.SunDiscAngularRadiusRadians = m_config.Sphere.SunDiscAngularRadiusRadians;
        push.SunDiscIntensity            = m_config.Sphere.SunDiscIntensity * m_config.EnvironmentIntensity;
        push.HorizonSharpness            = m_config.Sphere.HorizonSharpness;
        push.ShowSunDisc                 = m_config.Sphere.ShowSunDisc && m_celestial.IsAvailable && m_celestial.IsValid() ? 1.0f : 0.0f;

        command_buffer->SetViewport(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->SetScissor(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->BindPipeline(graphic_pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
