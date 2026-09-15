#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyCompositePass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        constexpr float      kAerialMaxDistance = 96.0f;

        TextureSpecification MakeCompositionSpecification(uint32_t width, uint32_t height)
        {
            TextureSpecification specification  = {};
            specification.IsUsageSampled        = true;
            specification.IsUsageStorage        = false;
            specification.IsUsageTransfert      = false;
            specification.IsUsageTransferSource = false;
            specification.IsRenderTargetSized   = true;
            specification.Width                 = width;
            specification.Height                = height;
            specification.Depth                 = 1;
            specification.BytePerPixel          = sizeof(uint16_t) * 4;
            specification.Format                = ImageFormat::R16G16B16A16_SFLOAT;
            specification.LoadOp                = LoadOperation::CLEAR;
            specification.ClearColor[0]         = 0.0f;
            specification.ClearColor[1]         = 0.0f;
            specification.ClearColor[2]         = 0.0f;
            specification.ClearColor[3]         = 1.0f;
            return specification;
        }
    } // namespace

    void SkyCompositePass::SetEnvironment(const Scenes::SkyEnvironmentSnapshot* snapshot, const Scenes::SkyConfig& presentation)
    {
        m_active = snapshot && snapshot->Config.IsAtmosphere() && snapshot->Atmosphere.Valid() && snapshot->CelestialLight.IsAvailable && snapshot->CelestialLight.IsValid();
        if (!m_active)
        {
            m_presentation = {};
            m_celestial    = {};
            m_atmosphere   = {};
            return;
        }

        m_presentation = presentation;
        m_celestial    = snapshot->CelestialLight;
        m_atmosphere   = snapshot->Atmosphere;
    }

    void SkyCompositePass::SetCameraDepthConvention(bool uses_reverse_z)
    {
        m_uses_reverse_z = uses_reverse_z;
    }

    void SkyCompositePass::SetCameraPosition(const Core::Maths::Vec3f& position)
    {
        m_camera_pos = position;
    }

    bool SkyCompositePass::IsViewActive() const
    {
        return m_active;
    }

    bool SkyCompositePass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        if (!device || !res_builder || !IsViewActive() || frame_context.RenderWidth == 0 || frame_context.RenderHeight == 0)
            return false;

        res_builder->ReadTexture(RendererResourceName::FrameHdrColorRenderTargetName, "OpaqueSceneColor");
        res_builder->ReadTexture(RendererResourceName::FrameDepthRenderTargetName, "SceneDepth");
        res_builder->ReadTexture(RendererResourceName::FrameSkyViewLutName, "SkyViewLut");
        res_builder->ReadTexture(RendererResourceName::FrameAerialInscatterName, "AerialInscatter");
        res_builder->ReadTexture(RendererResourceName::FrameAerialTransmittanceName, "AerialTransmittance");
        res_builder->ReadTexture(res_builder->ImportTexture("SkyCompositeTransmittance", m_atmosphere.Transmittance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "AtmosphereTransmittance");
        res_builder->WriteColorAttachment(RendererResourceName::FrameHdrCompositedRenderTargetName, MakeCompositionSpecification(frame_context.RenderWidth, frame_context.RenderHeight));
        return true;
    }

    GraphicsPipelineDesc SkyCompositePass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        GraphicsPipelineDesc description          = {};
        description.DebugName                     = "Sky-Composite-Pipeline";
        description.EnableDepthTest               = false;
        description.EnableDepthWrite              = false;
        description.EnableBlending                = false;
        description.ShaderSpecificationValue.Name = "sky_atmosphere_composite";
        return description;
    }

    void SkyCompositePass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr const res_inspector, RenderPasses::RenderPass* const pass)
    {
        if (!device || !scene || !res_inspector || !pass || !IsViewActive())
            return;

        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        graphic_pass->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
        const Textures::TextureHandle opaque_scene_color = res_inspector->GetRenderTarget(RendererResourceName::FrameHdrColorRenderTargetName);
        const Textures::TextureHandle scene_depth        = res_inspector->GetRenderTarget(RendererResourceName::FrameDepthRenderTargetName);
        if (opaque_scene_color.Valid())
            graphic_pass->SetTexture("OpaqueSceneColor", opaque_scene_color);
        if (scene_depth.Valid())
            graphic_pass->SetTexture("SceneDepth", scene_depth);
        graphic_pass->SetTexture("AtmosphereTransmittance", m_atmosphere.Transmittance);
        graphic_pass->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
        graphic_pass->SetSampler("SkyViewSampler", device->GlobalLinearWrapUClampToEdgeVSamplerImageInfo);
    }

    void SkyCompositePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsViewActive() || !pass || !command_buffer || !framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;

        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(graphic_pass, framebuffer->Handle, false))
            return;
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool SkyCompositePass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsViewActive() || !device || !scene)
            return false;

        auto* const               graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        SkyCompositePushConstants push         = {};
        push.AerialMaxDistanceAndDepthClear[0] = kAerialMaxDistance;
        push.AerialMaxDistanceAndDepthClear[1] = m_uses_reverse_z ? 0.0f : 1.0f;
        push.AerialMaxDistanceAndDepthClear[2] = 1.0e-5f;
        push.AerialMaxDistanceAndDepthClear[3] = m_presentation.Atmosphere.WorldUnitsPerMeter > 0.0f ? m_presentation.Atmosphere.WorldUnitsPerMeter * 1000.0f : 1.0f;
        push.PlanetCenterRelative[0]           = (m_presentation.Atmosphere.PlanetCenterWorld[0] - m_camera_pos.x) / push.AerialMaxDistanceAndDepthClear[3];
        push.PlanetCenterRelative[1]           = (m_presentation.Atmosphere.PlanetCenterWorld[1] - m_camera_pos.y) / push.AerialMaxDistanceAndDepthClear[3];
        push.PlanetCenterRelative[2]           = (m_presentation.Atmosphere.PlanetCenterWorld[2] - m_camera_pos.z) / push.AerialMaxDistanceAndDepthClear[3];
        push.AtmosphereRadiiAndPadding[0]      = m_presentation.Atmosphere.PlanetRadiusKilometers;
        push.AtmosphereRadiiAndPadding[1]      = m_presentation.Atmosphere.AtmosphereRadiusKilometers;
        push.SunDirectionAndRadius[0]          = m_celestial.DirectionToLight[0];
        push.SunDirectionAndRadius[1]          = m_celestial.DirectionToLight[1];
        push.SunDirectionAndRadius[2]          = m_celestial.DirectionToLight[2];
        push.SunDirectionAndRadius[3]          = m_presentation.Atmosphere.SunAngularRadiusRadians;
        const float sun_radiance               = Scenes::ConvertSunIlluminanceToSceneRadiance(m_presentation.Atmosphere.SunIlluminanceLux) * m_presentation.EnvironmentIntensity;
        push.SunRadianceAndAvailability[0]     = sun_radiance * m_presentation.EnvironmentTint[0];
        push.SunRadianceAndAvailability[1]     = sun_radiance * m_presentation.EnvironmentTint[1];
        push.SunRadianceAndAvailability[2]     = sun_radiance * m_presentation.EnvironmentTint[2];
        push.SunRadianceAndAvailability[3]     = m_celestial.IsAvailable ? 1.0f : 0.0f;

        command_buffer->SetViewport(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->SetScissor(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->BindPipeline(graphic_pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
        command_buffer->PushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
