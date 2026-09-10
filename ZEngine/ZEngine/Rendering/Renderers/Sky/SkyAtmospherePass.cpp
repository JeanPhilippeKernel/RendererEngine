#include <ZEngine/Rendering/Primitives/MemoryBarrier.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Renderers/Sky/SkyAtmospherePass.h>

using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Rendering::Primitives;

namespace ZEngine::Rendering::Renderers
{
    void SkyAtmospherePass::Setup(Hardwares::VulkanDevicePtr const device, cstring /*name*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        uint32_t w = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t h = device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.Width = w, .Height = h, .Format = ImageFormat::R8G8B8A8_UNORM, .LoadOp = LoadOperation::LOAD});
    }

    void SkyAtmospherePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (!m_luts_initialized)
        {
            InitLUTs(device);
            InitComputePipelines(device);
            m_luts_initialized = true;
        }

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("SkyAtmosphere-Combine-Pipeline").SetInputBindingCount(0).EnablePipelineDepthTest(true).EnablePipelineDepthWrite(false).PipelineDepthCompareOp(static_cast<uint32_t>(VK_COMPARE_OP_LESS_OR_EQUAL)).SetCullMode(static_cast<uint32_t>(VK_CULL_MODE_NONE)).UseShader("sky_combine").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }

        auto* gp = static_cast<RenderPasses::GraphicPass*>(*output_pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
        gp->SetDynamicUniform("UBAtmosphere", sizeof(SkyAtmosphereUBO));
    }

    void SkyAtmospherePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        SkyAtmosphereUBO ubo    = {};
        ubo.RayleighScattering  = Config.RayleighScattering;
        ubo.MieScattering       = Core::Maths::Vec4f(Config.MieScattering, Config.MieScattering, Config.MieScattering, 0.0f);
        ubo.MieAbsorption       = Core::Maths::Vec4f(Config.MieAbsorption, Config.MieAbsorption, Config.MieAbsorption, 0.0f);
        ubo.OzoneAbsorption     = Config.OzoneAbsorption;
        ubo.PlanetRadius        = Config.PlanetRadiusKm * 1000.0f;
        ubo.AtmosphereRadius    = Config.AtmosphereRadiusKm * 1000.0f;
        ubo.RayleighScaleHeight = Config.RayleighScaleHeight;
        ubo.MieScaleHeight      = Config.MieScaleHeight;
        ubo.MieAnisotropy       = Config.MieAnisotropy;
        ubo.OzoneLayerCentre    = Config.OzoneLayerCentre;
        ubo.OzoneLayerWidth     = Config.OzoneLayerWidth;
        ubo.SunIlluminanceScale = Config.SunIlluminanceScale;
        ubo.SunDirection        = Config.SunDirection;
        ubo.SunAngularRadius    = Config.SunAngularRadiusDeg;
        ubo.CameraPositionKm    = Core::Maths::Vec4f(0.0f, 0.0f, 0.0f, 0.0f);

        auto& heap              = device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index];
        auto  atmo_alloc        = heap.Push(&ubo, sizeof(SkyAtmosphereUBO), device->MinUniformBufferOffsetAlignment());
        m_atmo_heap_offset      = atmo_alloc.Offset;

        if (LUTsDirty && m_luts_initialized)
        {
            DispatchLUTs(device, command_buffer);
            LUTsDirty = false;
        }

        auto*    gp             = static_cast<RenderPasses::GraphicPass*>(pass);
        uint32_t dyn_offsets[2] = {scene ? scene->CameraHeapOffset : 0u, m_atmo_heap_offset};

        command_buffer->BeginRenderPass(gp, framebuffer->Handle, false);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, dyn_offsets, 2u);
        command_buffer->Draw(3, 1, 0, 0);
        command_buffer->EndRenderPass();
    }

    void SkyAtmospherePass::Deinitialize(Hardwares::VulkanDevicePtr const device)
    {
        m_transmittance_pipeline.Dispose();
        m_multiscatter_pipeline.Dispose();
        m_skyview_pipeline.Dispose();

        if (m_transmittance_lut.Valid())
            device->DestroyTexture(m_transmittance_lut);
        if (m_multiscatter_lut.Valid())
            device->DestroyTexture(m_multiscatter_lut);
        if (m_skyview_lut.Valid())
            device->DestroyTexture(m_skyview_lut);

        m_luts_initialized = false;
    }

    void SkyAtmospherePass::InitLUTs(Hardwares::VulkanDevice* device)
    {
        auto make_spec = [](uint32_t w, uint32_t h, ImageFormat fmt) {
            TextureSpecification s = {};
            s.Width                = w;
            s.Height               = h;
            s.Format               = fmt;
            s.IsUsageStorage       = true;
            s.IsUsageSampled       = true;
            s.IsUsageTransfert     = false;
            s.LoadOp               = LoadOperation::CLEAR;
            return s;
        };
        m_transmittance_lut = device->CreateTexture(make_spec(256, 64, ImageFormat::R11G11B10_UFLOAT));
        m_multiscatter_lut  = device->CreateTexture(make_spec(32, 32, ImageFormat::R16G16B16A16_SFLOAT));
        m_skyview_lut       = device->CreateTexture(make_spec(192, 108, ImageFormat::R16G16B16A16_SFLOAT));
    }

    void SkyAtmospherePass::InitComputePipelines(Hardwares::VulkanDevice* device)
    {
        m_transmittance_pipeline.Initialize(device, "sky_transmittance");
        m_multiscatter_pipeline.Initialize(device, "sky_multiscatter");
        m_skyview_pipeline.Initialize(device, "sky_skyview");

        if (m_transmittance_pipeline.Shader)
            m_transmittance_pipeline.Bake();
        if (m_multiscatter_pipeline.Shader)
            m_multiscatter_pipeline.Bake();
        if (m_skyview_pipeline.Shader)
            m_skyview_pipeline.Bake();
    }

    void SkyAtmospherePass::DispatchLUTs(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* cmd)
    {
        if (m_transmittance_pipeline.Handle == VK_NULL_HANDLE || m_multiscatter_pipeline.Handle == VK_NULL_HANDLE || m_skyview_pipeline.Handle == VK_NULL_HANDLE)
            return;

        const MemoryBarrier compute_to_compute{
            MemoryBarrierSpecification{
                                       .SourceStageMask       = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       .DestinationStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       .SourceAccessMask      = VK_ACCESS_SHADER_WRITE_BIT,
                                       .DestinationAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                       }
        };

        const MemoryBarrier compute_to_fragment{
            MemoryBarrierSpecification{
                                       .SourceStageMask       = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       .DestinationStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                       .SourceAccessMask      = VK_ACCESS_SHADER_WRITE_BIT,
                                       .DestinationAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                       }
        };

        // Transmittance: 256x64, local_size 8x8x1 → (32, 8, 1)
        cmd->BindPipeline(&m_transmittance_pipeline);
        cmd->Dispatch(32, 8, 1);
        cmd->PipelineBarrier(compute_to_compute);

        // Multiscatter: 32x32, local_size 1x1x64 → (32, 32, 1)
        cmd->BindPipeline(&m_multiscatter_pipeline);
        cmd->Dispatch(32, 32, 1);
        cmd->PipelineBarrier(compute_to_compute);

        // Sky-view: 192x108, local_size 8x8x1 → (24, 14, 1)
        cmd->BindPipeline(&m_skyview_pipeline);
        cmd->Dispatch(24, 14, 1);
        cmd->PipelineBarrier(compute_to_fragment);
    }
} // namespace ZEngine::Rendering::Renderers
