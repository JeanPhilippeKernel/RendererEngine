#include <ZEngine/Rendering/Primitives/ImageMemoryBarrier.h>
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
        // Sky-view LUT is sampled via the global TextureArray — no explicit SetTexture needed.
    }

    void SkyAtmospherePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        SkyAtmosphereUBO ubo      = {};
        ubo.RayleighScattering    = Config.RayleighScattering;
        ubo.MieScattering         = Core::Maths::Vec4f(Config.MieScattering, Config.MieScattering, Config.MieScattering, 0.0f);
        ubo.MieAbsorption         = Core::Maths::Vec4f(Config.MieAbsorption, Config.MieAbsorption, Config.MieAbsorption, 0.0f);
        ubo.OzoneAbsorption       = Config.OzoneAbsorption;
        ubo.PlanetRadius          = Config.PlanetRadiusKm * 1000.0f;
        ubo.AtmosphereRadius      = Config.AtmosphereRadiusKm * 1000.0f;
        ubo.RayleighScaleHeight   = Config.RayleighScaleHeight;
        ubo.MieScaleHeight        = Config.MieScaleHeight;
        ubo.MieAnisotropy         = Config.MieAnisotropy;
        ubo.OzoneLayerCentre      = Config.OzoneLayerCentre;
        ubo.OzoneLayerWidth       = Config.OzoneLayerWidth;
        ubo.SunIlluminanceScale   = Config.SunIlluminanceScale;
        ubo.SunDirection          = Config.SunDirection;
        ubo.SunAngularRadius      = Config.SunAngularRadiusDeg;
        ubo.CameraPositionKm      = Core::Maths::Vec4f(0.0f, 0.0f, 0.0f, 0.0f);
        ubo.TransmittanceLUTIndex = m_transmittance_lut.Valid() ? m_transmittance_lut.Index : 0u;
        ubo.MultiscatterLUTIndex  = m_multiscatter_lut.Valid() ? m_multiscatter_lut.Index : 0u;
        ubo.SkyviewLUTIndex       = m_skyview_lut.Valid() ? m_skyview_lut.Index : 0u;

        m_camera_heap_offset      = scene ? scene->CameraHeapOffset : 0u;

        auto& heap                = device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index];
        auto  atmo_alloc          = heap.Push(&ubo, sizeof(SkyAtmosphereUBO), device->MinUniformBufferOffsetAlignment());
        m_atmo_heap_offset        = atmo_alloc.Offset;

        if (m_luts_initialized)
        {
            DispatchLUTs(device, command_buffer);
            // Frame 0: bindless array is updated in Present() after this Execute().
            // Set m_combine_ready on the NEXT frame so the combine pass runs only
            // when TextureArray[SkyviewLUTIndex] is guaranteed to be valid.
            if (!m_combine_ready)
            {
                m_combine_ready = true;
                LUTsDirty       = false;
                return;
            }
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

        InitLUTDescriptors(device);
    }

    static VkImageView GetTextureView(Hardwares::VulkanDevice* device, Textures::TextureHandle handle)
    {
        auto* tex = device->GlobalTextures.Access(handle);
        if (!tex)
            return VK_NULL_HANDLE;
        auto* img = device->ImageBufferManager.Access(tex->BufferHandle);
        return img ? img->GetImageViewHandle() : VK_NULL_HANDLE;
    }

    static void WriteStorageImage(Hardwares::VulkanDevice* device, VkDescriptorSet set, uint32_t binding, VkImageView view)
    {
        if (view == VK_NULL_HANDLE || set == VK_NULL_HANDLE)
            return;
        VkDescriptorImageInfo img_info = {
            .sampler     = VK_NULL_HANDLE,
            .imageView   = view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = set,
            .dstBinding      = binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo      = &img_info,
        };
        vkUpdateDescriptorSets(device->LogicalDevice, 1, &write, 0, nullptr);
    }

    void SkyAtmospherePass::InitLUTDescriptors(Hardwares::VulkanDevice* device)
    {
        uint32_t    frames             = device->SwapchainPtr->BufferredFrameCount;
        VkImageView transmittance_view = GetTextureView(device, m_transmittance_lut);
        VkImageView multiscatter_view  = GetTextureView(device, m_multiscatter_lut);
        VkImageView skyview_view       = GetTextureView(device, m_skyview_lut);

        // Each compute pipeline uses set 2 binding 0 as its write-only storage image output.
        // Input LUTs are sampled via the global TextureArray (set 1) — no sampler writes here.
        auto        write_output       = [&](Pipelines::ComputePipeline& pipeline, VkImageView view) {
            if (!pipeline.Shader)
                return;
            const auto* set2 = pipeline.Shader->DescriptorSetMap.find(2);
            if (set2)
                for (uint32_t f = 0; f < frames; ++f)
                    WriteStorageImage(device, (*set2)[f], 0, view);
        };

        write_output(m_transmittance_pipeline, transmittance_view);
        write_output(m_multiscatter_pipeline, multiscatter_view);
        write_output(m_skyview_pipeline, skyview_view);

        // Point the dynamic UBO descriptors (set 0) at the per-frame FrameHeap buffer.
        // The actual offset is supplied at dispatch time via BindDescriptorSets.
        auto write_dynamic_ubo = [&](Pipelines::ComputePipeline& pipeline, uint32_t binding, VkDeviceSize range) {
            if (!pipeline.Shader)
                return;
            const auto* set0 = pipeline.Shader->DescriptorSetMap.find(0);
            if (!set0)
                return;
            for (uint32_t f = 0; f < frames; ++f)
            {
                VkDescriptorBufferInfo buf_info = {.buffer = device->FrameHeaps[f].Handle, .offset = 0, .range = range};
                VkWriteDescriptorSet   write    = {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet          = (*set0)[f],
                    .dstBinding      = binding,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .pBufferInfo     = &buf_info,
                };
                vkUpdateDescriptorSets(device->LogicalDevice, 1, &write, 0, nullptr);
            }
        };

        // Atmosphere UBO at set 0 binding 1 for all three compute pipelines.
        write_dynamic_ubo(m_transmittance_pipeline, 1, sizeof(SkyAtmosphereUBO));
        write_dynamic_ubo(m_multiscatter_pipeline, 1, sizeof(SkyAtmosphereUBO));
        write_dynamic_ubo(m_skyview_pipeline, 1, sizeof(SkyAtmosphereUBO));
        // Sky-view also needs camera UBO at set 0 binding 0.
        write_dynamic_ubo(m_skyview_pipeline, 0, sizeof(UBOCameraLayout));

        // Push LUT textures into the global bindless TextureArray.
        if (m_transmittance_lut.Valid())
            device->TextureHandleToUpdates.Enqueue(m_transmittance_lut);
        if (m_multiscatter_lut.Valid())
            device->TextureHandleToUpdates.Enqueue(m_multiscatter_lut);
        if (m_skyview_lut.Valid())
            device->TextureHandleToUpdates.Enqueue(m_skyview_lut);
    }

    static void TransitionLUT(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* cmd, Textures::TextureHandle handle, Specifications::ImageLayout old_layout, Specifications::ImageLayout new_layout, VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
    {
        auto* tex = device->GlobalTextures.Access(handle);
        if (!tex)
            return;
        auto* img = device->ImageBufferManager.Access(tex->BufferHandle);
        if (!img)
            return;

        Specifications::ImageMemoryBarrierSpecification spec = {};
        spec.OldLayout                                       = old_layout;
        spec.NewLayout                                       = new_layout;
        spec.ImageHandle                                     = img->GetHandle();
        spec.SourceAccessMask                                = src_access;
        spec.DestinationAccessMask                           = dst_access;
        spec.ImageAspectMask                                 = VK_IMAGE_ASPECT_COLOR_BIT;
        spec.SourceStageMask                                 = static_cast<VkPipelineStageFlagBits>(src_stage);
        spec.DestinationStageMask                            = static_cast<VkPipelineStageFlagBits>(dst_stage);
        cmd->TransitionImageLayout(Primitives::ImageMemoryBarrier{spec});
    }

    void SkyAtmospherePass::DispatchLUTs(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* cmd)
    {
        if (m_skyview_pipeline.Handle == VK_NULL_HANDLE)
            return;

        const bool rebuild_static = LUTsDirty && m_transmittance_pipeline.Handle != VK_NULL_HANDLE && m_multiscatter_pipeline.Handle != VK_NULL_HANDLE;

        uint32_t   frame_index    = device->SwapchainPtr->CurrentFrame->Index;

        // Sky-view transitions every frame; static LUTs only when rebuild_static.
        if (rebuild_static)
        {
            TransitionLUT(device, cmd, m_transmittance_lut, ImageLayout::UNDEFINED, ImageLayout::GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            TransitionLUT(device, cmd, m_multiscatter_lut, ImageLayout::UNDEFINED, ImageLayout::GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        TransitionLUT(device, cmd, m_skyview_lut, ImageLayout::UNDEFINED, ImageLayout::GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

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

        if (rebuild_static)
        {
            // Transmittance: 256x64, local_size 8x8x1 → (32, 8, 1)
            cmd->BindPipeline(&m_transmittance_pipeline);
            cmd->BindDescriptorSets(frame_index, &m_atmo_heap_offset, 1u);
            cmd->Dispatch(32, 8, 1);
            cmd->PipelineBarrier(compute_to_compute);
            TransitionLUT(device, cmd, m_transmittance_lut, ImageLayout::GENERAL, ImageLayout::SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

            // Multiscatter: 32x32, local_size 1x1x64 → (32, 32, 1)
            cmd->BindPipeline(&m_multiscatter_pipeline);
            cmd->BindDescriptorSets(frame_index, &m_atmo_heap_offset, 1u);
            cmd->Dispatch(32, 32, 1);
            cmd->PipelineBarrier(compute_to_compute);
            TransitionLUT(device, cmd, m_multiscatter_lut, ImageLayout::GENERAL, ImageLayout::SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        // Sky-view: 192x108, local_size 8x8x1 → (24, 14, 1)
        // sky_skyview.comp reads both camera (binding 0) and atmosphere (binding 1) UBOs.
        uint32_t skyview_offsets[2] = {m_camera_heap_offset, m_atmo_heap_offset};
        cmd->BindPipeline(&m_skyview_pipeline);
        cmd->BindDescriptorSets(frame_index, skyview_offsets, 2u);
        cmd->Dispatch(24, 14, 1);
        // Transition sky-view GENERAL → SHADER_READ_ONLY before combine frag reads it
        TransitionLUT(device, cmd, m_skyview_lut, ImageLayout::GENERAL, ImageLayout::SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        cmd->PipelineBarrier(compute_to_fragment);
    }
} // namespace ZEngine::Rendering::Renderers
