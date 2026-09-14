#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Compute/SkyEnvironmentBakePass.h>
#include <algorithm>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        constexpr const char* kSourceRadianceName      = "SkyBakeSourceRadiance";
        constexpr const char* kDiffuseIrradianceName   = "SkyBakeDiffuseIrradiance";
        constexpr const char* kSpecularEnvironmentName = "SkyBakeSpecularEnvironment";
        constexpr uint32_t    kLocalSize               = 8;

        struct MipGenerationPushConstants
        {
            uint32_t SourceMip             = 0;
            uint32_t DestinationMip        = 0;
            uint32_t DestinationResolution = 1;
            uint32_t Padding               = 0;
        };

        struct DiffuseIrradiancePushConstants
        {
            uint32_t Resolution  = 0;
            uint32_t SampleCount = 0;
            uint32_t Padding[2]  = {};
        };

        struct SpecularPrefilterPushConstants
        {
            uint32_t Resolution     = 0;
            uint32_t MipLevel       = 0;
            uint32_t SampleCount    = 0;
            uint32_t SourceMipCount = 1;
            float    Roughness      = 0.0f;
            float    Padding[3]     = {};
        };

        static_assert(sizeof(MipGenerationPushConstants) == 16, "Sky mip push constants must match GLSL");
        static_assert(sizeof(DiffuseIrradiancePushConstants) == 16, "Diffuse irradiance push constants must match GLSL");
        static_assert(sizeof(SpecularPrefilterPushConstants) == 32, "Specular prefilter push constants must match GLSL");

        bool GetImage(Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle, Hardwares::ImageBuffer** out_image)
        {
            if (out_image)
                *out_image = nullptr;
            if (!device || !texture_handle.Valid())
                return false;

            auto* const texture = device->GlobalTextures.Access(texture_handle);
            auto* const image   = texture ? device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
            if (!image || image->GetHandle() == VK_NULL_HANDLE)
                return false;
            if (out_image)
                *out_image = image;
            return true;
        }

        uint32_t GetMipCount(Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle)
        {
            Hardwares::ImageBuffer* image = nullptr;
            return GetImage(device, texture_handle, &image) ? image->Specification.MipLevelCount : 0;
        }

        uint32_t GetResolution(Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle)
        {
            auto* const texture = device && texture_handle.Valid() ? device->GlobalTextures.Access(texture_handle) : nullptr;
            return texture ? texture->Width : 0;
        }

        VkImageSubresourceRange MakeColorRange(uint32_t mip_level, uint32_t mip_count)
        {
            VkImageSubresourceRange range = {};
            range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel            = mip_level;
            range.levelCount              = mip_count;
            range.baseArrayLayer          = 0;
            range.layerCount              = 6;
            return range;
        }

        void MakeShaderReadable(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* command_buffer, Textures::TextureHandle texture_handle)
        {
            Hardwares::ImageBuffer* image = nullptr;
            if (!GetImage(device, texture_handle, &image))
                return;

            VkImageMemoryBarrier2 transition   = {};
            transition.sType                   = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            transition.srcStageMask            = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            transition.srcAccessMask           = VK_ACCESS_2_SHADER_WRITE_BIT;
            transition.dstStageMask            = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            transition.dstAccessMask           = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            transition.oldLayout               = VK_IMAGE_LAYOUT_GENERAL;
            transition.newLayout               = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            transition.srcQueueFamilyIndex     = VK_QUEUE_FAMILY_IGNORED;
            transition.dstQueueFamilyIndex     = VK_QUEUE_FAMILY_IGNORED;
            transition.image                   = image->GetHandle();
            transition.subresourceRange        = MakeColorRange(0, image->Specification.MipLevelCount);

            VkDependencyInfo dependency        = {};
            dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers    = &transition;
            command_buffer->PipelineBarrier2(dependency);
            image->Layout = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
        }

        void MakeMipVisible(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* command_buffer, Textures::TextureHandle texture_handle, uint32_t mip_level)
        {
            Hardwares::ImageBuffer* image = nullptr;
            if (!GetImage(device, texture_handle, &image))
                return;

            VkImageMemoryBarrier2 barrier      = {};
            barrier.sType                      = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask               = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask              = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask               = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask              = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrier.oldLayout                  = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout                  = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex        = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex        = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                      = image->GetHandle();
            barrier.subresourceRange           = MakeColorRange(mip_level, 1);

            VkDependencyInfo dependency        = {};
            dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers    = &barrier;
            command_buffer->PipelineBarrier2(dependency);
        }

    } // namespace

    RGPassFlags SkyEnvironmentBakePass::GetPassFlags() const
    {
        return RGPassFlags::NeverCull;
    }

    Rendering::QueueType SkyEnvironmentBakePass::GetRequestedQueue() const
    {
        // Publication is synchronized through the render timeline. Keeping the
        // bake on this queue makes that ordering explicit on every topology.
        return Rendering::QueueType::GRAPHIC_QUEUE;
    }

    void SkyEnvironmentBakePass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        m_compute_pass = pass ? static_cast<RenderPasses::ComputePass*>(pass) : nullptr;
        if (m_compute_pass && device)
            m_compute_pass->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    bool SkyEnvironmentBakePass::IsActive(Scenes::SkyEnvironmentBakeStage stage) const
    {
        return m_environment && m_environment->CanRecordGpuBakeStage() && m_environment->GetActiveBakeStage() == stage && m_environment->GetActiveBakeSource().Valid();
    }

    cstring SkyEnvironmentMipGenerationPass::GetShaderName() const
    {
        return "sky_environment_mip_generation";
    }

    uint32_t SkyEnvironmentMipGenerationPass::GetPushConstantSize() const
    {
        return sizeof(MipGenerationPushConstants);
    }

    bool SkyEnvironmentMipGenerationPass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::SourceMipChain);
    }

    void SkyEnvironmentMipGenerationPass::RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Textures::TextureHandle source = m_environment->GetActiveBakeSource();
        auto* const                   rrm    = m_environment && device && device->RRM ? static_cast<Rendering::RenderResourceManager*>(device->RRM) : nullptr;
        if (const auto* ticket = rrm ? rrm->FindStreamingUploadTicket(source) : nullptr)
            res_builder->ImportStreamingTexture(kSourceRadianceName, *ticket);
        else
            res_builder->ImportTexture(kSourceRadianceName, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        res_builder->ReadWriteStorageImage(kSourceRadianceName, "Destination");
    }

    void SkyEnvironmentMipGenerationPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass)
    {
        SkyEnvironmentBakePass::Prepare(device, scene, res_inspector, pass);
        if (!m_compute_pass || !m_environment)
            return;
        m_compute_pass->SetTexture("SourceRadiance", m_environment->GetActiveBakeSource(), VK_IMAGE_LAYOUT_GENERAL);
    }

    void SkyEnvironmentMipGenerationPass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::SourceMipChain) || !m_compute_pass)
            return;

        const Textures::TextureHandle source    = m_environment->GetActiveBakeSource();
        const uint32_t                mip_count = GetMipCount(device, source);
        if (mip_count == 0)
            return;

        for (uint32_t mip = 1; mip < mip_count; ++mip)
        {
            const uint32_t resolution = std::max(1u, GetResolution(device, source) >> mip);
            m_compute_pass->SetStorageImage("Destination", source, MakeColorRange(mip, 1));
            command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
            const MipGenerationPushConstants push = {.SourceMip = mip - 1, .DestinationMip = mip, .DestinationResolution = resolution};
            command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 6);
            MakeMipVisible(device, command_buffer, source, mip);
        }
        MakeShaderReadable(device, command_buffer, source);
        if (const auto* bake = m_environment->GetActiveBake())
            m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::SourceMipChain);
    }

    cstring SkyEnvironmentDiffuseIrradiancePass::GetShaderName() const
    {
        return "sky_environment_diffuse_irradiance";
    }

    uint32_t SkyEnvironmentDiffuseIrradiancePass::GetPushConstantSize() const
    {
        return sizeof(DiffuseIrradiancePushConstants);
    }

    bool SkyEnvironmentDiffuseIrradiancePass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::DiffuseIrradiance) && m_environment->GetActiveBakeLighting().DiffuseIrradiance.Valid();
    }

    void SkyEnvironmentDiffuseIrradiancePass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Textures::TextureHandle source = m_environment->GetActiveBakeSource();
        const Textures::TextureHandle output = m_environment->GetActiveBakeLighting().DiffuseIrradiance;
        res_builder->ReadTexture(res_builder->ImportTexture(kSourceRadianceName, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "SourceRadiance");
        res_builder->ImportTexture(kDiffuseIrradianceName, output, VK_IMAGE_LAYOUT_UNDEFINED);
        res_builder->ReadWriteStorageImage(kDiffuseIrradianceName, "Destination");
    }

    void SkyEnvironmentDiffuseIrradiancePass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::DiffuseIrradiance) || !m_compute_pass)
            return;

        const EnvironmentLightingBakeSettings& bake_settings = m_environment->GetActiveBakeLighting().BakeSettings;
        const Textures::TextureHandle          output        = m_environment->GetActiveBakeLighting().DiffuseIrradiance;
        const uint32_t                         resolution    = GetResolution(device, output);
        if (resolution == 0)
            return;

        m_compute_pass->SetStorageImage("Destination", output, MakeColorRange(0, 1));
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        const DiffuseIrradiancePushConstants push = {.Resolution = resolution, .SampleCount = bake_settings.DiffuseSampleCount};
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 6);
        MakeShaderReadable(device, command_buffer, output);
        if (const auto* bake = m_environment->GetActiveBake())
            m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::DiffuseIrradiance);
    }

    cstring SkyEnvironmentSpecularPrefilterPass::GetShaderName() const
    {
        return "sky_environment_specular_prefilter";
    }

    uint32_t SkyEnvironmentSpecularPrefilterPass::GetPushConstantSize() const
    {
        return sizeof(SpecularPrefilterPushConstants);
    }

    bool SkyEnvironmentSpecularPrefilterPass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::SpecularEnvironment) && m_environment->GetActiveBakeLighting().SpecularEnvironment.Valid();
    }

    void SkyEnvironmentSpecularPrefilterPass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Textures::TextureHandle source = m_environment->GetActiveBakeSource();
        const Textures::TextureHandle output = m_environment->GetActiveBakeLighting().SpecularEnvironment;
        res_builder->ReadTexture(res_builder->ImportTexture(kSourceRadianceName, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "SourceRadiance");
        res_builder->ImportTexture(kSpecularEnvironmentName, output, VK_IMAGE_LAYOUT_UNDEFINED);
        res_builder->ReadWriteStorageImage(kSpecularEnvironmentName, "Destination");
    }

    void SkyEnvironmentSpecularPrefilterPass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::SpecularEnvironment) || !m_compute_pass)
            return;

        const EnvironmentLightingBakeSettings& bake_settings   = m_environment->GetActiveBakeLighting().BakeSettings;
        const Textures::TextureHandle          source          = m_environment->GetActiveBakeSource();
        const Textures::TextureHandle          output          = m_environment->GetActiveBakeLighting().SpecularEnvironment;
        const uint32_t                         mip_count       = GetMipCount(device, output);
        const uint32_t                         base_resolution = GetResolution(device, output);
        const uint32_t                         source_mips     = GetMipCount(device, source);
        if (mip_count == 0 || base_resolution == 0 || source_mips == 0)
            return;

        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            const uint32_t resolution = std::max(1u, base_resolution >> mip);
            m_compute_pass->SetStorageImage("Destination", output, MakeColorRange(mip, 1));
            command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
            const SpecularPrefilterPushConstants push = {
                .Resolution     = resolution,
                .MipLevel       = mip,
                .SampleCount    = bake_settings.SpecularSampleCount,
                .SourceMipCount = source_mips,
                .Roughness      = mip_count > 1 ? static_cast<float>(mip) / static_cast<float>(mip_count - 1) : 0.0f,
            };
            command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 6);
        }
        MakeShaderReadable(device, command_buffer, output);
        if (const auto* bake = m_environment->GetActiveBake())
            m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::SpecularEnvironment);
    }
} // namespace ZEngine::Rendering::Renderers
