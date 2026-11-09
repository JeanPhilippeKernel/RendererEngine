#include <ZEngine/Engine.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Profiling/Profiling.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Compute/FrustumCullingPass.h>
#include <ZEngine/Rendering/Renderers/Compute/SkyAtmosphereViewPass.h>
#include <ZEngine/Rendering/Renderers/Compute/SkyEnvironmentBakePass.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/DepthPrePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/EnvironmentBackgroundPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridLabelPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/ObjectIdPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SelectionOutlinePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyCompositePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkySpherePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/ToneMappingPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/TransparentPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <algorithm>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        constexpr VkFormat             SkyLightingFormat         = VK_FORMAT_R16G16B16A16_SFLOAT;
        constexpr VkFormat             HDRISourceFormat          = VK_FORMAT_R32G32B32A32_SFLOAT;
        constexpr VkFormatFeatureFlags RequiredSkyFormatFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        constexpr VkImageUsageFlags    RequiredSkyImageUsage     = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

        uint32_t                       GetFullMipCount(uint32_t resolution)
        {
            uint32_t mip_count = 1;
            while (resolution > 1)
            {
                resolution >>= 1;
                ++mip_count;
            }
            return mip_count;
        }

        uint64_t EstimateTextureBytes(uint32_t width, uint32_t height, uint32_t depth, uint32_t bytes_per_pixel, uint32_t layers, uint32_t mip_count)
        {
            uint64_t bytes = 0;
            for (uint32_t mip = 0; mip < mip_count; ++mip)
            {
                const uint32_t mip_width   = std::max(1u, width >> mip);
                const uint32_t mip_height  = std::max(1u, height >> mip);
                const uint32_t mip_depth   = std::max(1u, depth >> mip);
                bytes                     += static_cast<uint64_t>(mip_width) * mip_height * mip_depth * bytes_per_pixel * layers;
            }
            return bytes;
        }

        uint64_t EstimateSkyLightingBytes(const EnvironmentLightingBakeSettings& bake_settings)
        {
            constexpr uint32_t rgba16f_bytes_per_pixel = sizeof(uint16_t) * 4;
            return EstimateTextureBytes(bake_settings.DiffuseResolution, bake_settings.DiffuseResolution, 1, rgba16f_bytes_per_pixel, 6, 1) + EstimateTextureBytes(bake_settings.SpecularResolution, bake_settings.SpecularResolution, 1, rgba16f_bytes_per_pixel, 6, GetFullMipCount(bake_settings.SpecularResolution));
        }

        uint64_t EstimateAtmosphereBakeBytes(const EnvironmentLightingBakeSettings& bake_settings)
        {
            constexpr uint32_t rgba16f_bytes_per_pixel = sizeof(uint16_t) * 4;
            constexpr uint32_t atmosphere_lut_bytes    = 256 * 64 * rgba16f_bytes_per_pixel + 32 * 32 * rgba16f_bytes_per_pixel;
            return atmosphere_lut_bytes + EstimateTextureBytes(bake_settings.SourceRadianceResolution, bake_settings.SourceRadianceResolution, 1, rgba16f_bytes_per_pixel, 6, GetFullMipCount(bake_settings.SourceRadianceResolution)) + EstimateSkyLightingBytes(bake_settings);
        }

        uint64_t EstimateHDRIBakeBytes(const EnvironmentLightingBakeSettings& bake_settings, uint32_t face_resolution)
        {
            constexpr uint32_t rgba32f_bytes_per_pixel = sizeof(float) * 4;
            return EstimateTextureBytes(face_resolution, face_resolution, 1, rgba32f_bytes_per_pixel, 6, GetFullMipCount(face_resolution)) + EstimateSkyLightingBytes(bake_settings);
        }

        uint64_t GetTextureBytes(Hardwares::VulkanDevice* device, Textures::TextureHandle handle)
        {
            if (!device || !handle.Valid())
                return 0;
            const Textures::Texture* const texture = device->GlobalTextures.Access(handle);
            return texture ? static_cast<uint64_t>(texture->BufferSize) : 0;
        }

        void SetCapabilityReason(cstring* out_reason, cstring reason)
        {
            if (out_reason)
                *out_reason = reason;
        }

        bool SupportsSkyFormat(VkPhysicalDevice device, VkFormat format)
        {
            VkFormatProperties properties = {};
            vkGetPhysicalDeviceFormatProperties(device, format, &properties);
            return (properties.optimalTilingFeatures & RequiredSkyFormatFeatures) == RequiredSkyFormatFeatures;
        }

        bool SupportsSkyImage(VkPhysicalDevice device, VkFormat format, VkImageType type, VkImageCreateFlags flags, uint32_t width, uint32_t height, uint32_t depth, uint32_t layers)
        {
            VkImageFormatProperties properties = {};
            return vkGetPhysicalDeviceImageFormatProperties(device, format, type, VK_IMAGE_TILING_OPTIMAL, RequiredSkyImageUsage, flags, &properties) == VK_SUCCESS && properties.maxExtent.width >= width && properties.maxExtent.height >= height && properties.maxExtent.depth >= depth && properties.maxArrayLayers >= layers;
        }
    } // namespace

    GraphicRenderer::GraphicRenderer() {}
    GraphicRenderer::~GraphicRenderer() {}

    void GraphicRenderer::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device                       = device;
        RenderGraph                  = ZPushStructCtor<Renderers::RenderGraph>(Device->Arena);
        m_scene_pipeline_preparation = RenderGraph;
        RenderSceneData              = ZPushStructCtor<Scenes::SceneData>(Device->Arena);
        ZENGINE_VALIDATE_ASSERT(Device->SwapchainPtr->BufferredFrameCount <= Scenes::SceneData::MAX_FRAMES_IN_FLIGHT, "SceneData buffers must cover every buffered frame")
        constexpr const char*  transform_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]       = {"TransformStorageBuffer[0]", "TransformStorageBuffer[1]", "TransformStorageBuffer[2]"};
        constexpr const char*  render_data_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]     = {"RenderDataStorageBuffer[0]", "RenderDataStorageBuffer[1]", "RenderDataStorageBuffer[2]"};
        constexpr const char*  material_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]        = {"MaterialStorageBuffer[0]", "MaterialStorageBuffer[1]", "MaterialStorageBuffer[2]"};
        constexpr const char*  light_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]           = {"LightStorageBuffer[0]", "LightStorageBuffer[1]", "LightStorageBuffer[2]"};
        constexpr const char*  selection_token_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT] = {"SelectionTokenStateBuffer[0]", "SelectionTokenStateBuffer[1]", "SelectionTokenStateBuffer[2]"};
        constexpr const char*  culling_input_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]   = {"FrustumCullingInput[0]", "FrustumCullingInput[1]", "FrustumCullingInput[2]"};
        constexpr const char*  culled_indirect_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT] = {"FrustumCulledIndirect[0]", "FrustumCulledIndirect[1]", "FrustumCulledIndirect[2]"};
        constexpr VkDeviceSize culling_input_size                                             = Scenes::SceneData::MAX_DRAW_COMMANDS * sizeof(Scenes::FrustumCullingInput);
        constexpr VkDeviceSize culled_indirect_size                                           = Scenes::SceneData::MAX_DRAW_COMMANDS * sizeof(VkDrawIndirectCommand);
        for (uint32_t i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
        {
            RenderSceneData->TransformBuffers[i]           = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, transform_names[i]);
            RenderSceneData->RenderDataBuffers[i]          = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, render_data_names[i]);
            RenderSceneData->MaterialBuffers[i]            = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, material_names[i]);
            RenderSceneData->LightBuffers[i]               = Device->GpuMem.AllocateBuffer(sizeof(Scenes::LightArrayUBO), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, light_names[i]);
            RenderSceneData->SelectionTokenStateBuffers[i] = Device->GpuMem.AllocateBuffer((Scenes::SceneData::MAX_DRAW_COMMANDS + 1) * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, selection_token_names[i]);
            RenderSceneData->CullingInputBuffers[i]        = Device->GpuMem.AllocateBuffer(culling_input_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, culling_input_names[i]);
            RenderSceneData->CulledIndirectBuffers[i]      = Device->GpuMem.AllocateBuffer(culled_indirect_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, Core::Memory::GpuMemoryDomain::DeviceGeometry, culled_indirect_names[i]);
        }

        /*
         * Renderer Passes
         */
        auto scene_depth_prepass         = ZPushStructCtor<DepthPrePass>(Device->Arena);
        auto frustum_culling_pass        = ZPushStructCtor<FrustumCullingPass>(Device->Arena);
        auto gbuffer_pass                = ZPushStructCtor<GbufferPass>(Device->Arena);
        auto object_id_pass              = ZPushStructCtor<ObjectIdPass>(Device->Arena);
        auto lighting_pass               = ZPushStructCtor<LightingPass>(Device->Arena);
        m_transparent_pass               = ZPushStructCtor<TransparentPass>(Device->Arena);
        auto environment_background_pass = ZPushStructCtor<EnvironmentBackgroundPass>(Device->Arena);
        auto sky_sphere_pass             = ZPushStructCtor<SkySpherePass>(Device->Arena);
        auto sky_view_lut_pass           = ZPushStructCtor<SkyViewLutPass>(Device->Arena);
        auto aerial_pass                 = ZPushStructCtor<AerialPerspectivePass>(Device->Arena);
        auto sky_composite_pass          = ZPushStructCtor<SkyCompositePass>(Device->Arena);
        auto grid_pass                   = ZPushStructCtor<GridPass>(Device->Arena);
        auto tone_mapping_pass           = ZPushStructCtor<ToneMappingPass>(Device->Arena);
        auto grid_label_pass             = ZPushStructCtor<GridLabelPass>(Device->Arena);
        m_grid_label_pass                = grid_label_pass;
        auto gizmo_pass                  = ZPushStructCtor<GizmoPass>(Device->Arena);
        gizmo_pass->Initialize(Device);
        m_gizmo_pass                = gizmo_pass;
        auto selection_outline_pass = ZPushStructCtor<SelectionOutlinePass>(Device->Arena);

        RenderGraph->Initialize(Device, RenderSceneData);
        RenderGraph->ImportBuffer(RendererBufferName::Transform, &RenderSceneData->TransformBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::RenderData, &RenderSceneData->RenderDataBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::Material, &RenderSceneData->MaterialBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::Light, &RenderSceneData->LightBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::SelectionTokenState, &RenderSceneData->SelectionTokenStateBuffers[0]);
        // Setup needs one valid imported view to establish declarations. DrawScene
        // updates both pointers to the active frame's views before Execute(),
        // which is when RenderGraph stamps its runtime barriers.
        RenderGraph->ImportBuffer(RendererBufferName::CullingInput, &RenderSceneData->CullingInputBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::CulledIndirect, &RenderSceneData->CulledIndirectBuffers[0]);
        ZENGINE_VALIDATE_ASSERT(Device->RRM != nullptr, "Graphic renderer requires a render resource manager")
        auto* const rrm                  = static_cast<Rendering::RenderResourceManager*>(Device->RRM);
        const auto  fallback_environment = rrm->GetOrCreateFallbackCubemap();
        const auto  fallback_lighting    = rrm->GetOrCreateFallbackEnvironmentLighting();
        ZENGINE_VALIDATE_ASSERT(fallback_environment.Valid(), "Sky environment fallback source creation failed")
        ZENGINE_VALIDATE_ASSERT(fallback_lighting.Valid(), "Sky environment fallback lighting creation failed")
        m_sky_environment.Initialize(fallback_environment, fallback_lighting, Device->EnvironmentLightingBakeSettings);
        const uint64_t fallback_memory_bytes = GetTextureBytes(Device, fallback_environment) + GetTextureBytes(Device, fallback_lighting.DiffuseIrradiance) + GetTextureBytes(Device, fallback_lighting.SpecularEnvironment) + GetTextureBytes(Device, fallback_lighting.BrdfIntegrationLut);
        m_sky_environment.ConfigureMemoryBudget(Device->EnvironmentLightingMemoryBudget, fallback_memory_bytes);
        m_object_id_pass                           = object_id_pass;
        m_lighting_pass                            = lighting_pass;
        m_grid_pass                                = grid_pass;
        m_environment_background_pass              = environment_background_pass;
        m_sky_sphere_pass                          = sky_sphere_pass;
        m_sky_view_lut_pass                        = sky_view_lut_pass;
        m_aerial_perspective_pass                  = aerial_pass;
        m_sky_composite_pass                       = sky_composite_pass;
        m_tone_mapping_pass                        = tone_mapping_pass;
        m_selection_outline_pass                   = selection_outline_pass;
        m_environment_lighting_resources_supported = SupportsEnvironmentLightingResources(Device->EnvironmentLightingBakeSettings, &m_environment_lighting_unavailable_reason);
        m_atmosphere_bake_resources_supported      = SupportsAtmosphereBakeResources(Device->EnvironmentLightingBakeSettings, &m_atmosphere_bake_unavailable_reason);
        m_atmosphere_view_resources_supported      = SupportsAtmosphereViewResources(&m_atmosphere_view_unavailable_reason);
        if (!m_environment_lighting_resources_supported)
            ZENGINE_CORE_WARN("[SkyEnvironment] HDRI and atmosphere IBL are disabled: {}", m_environment_lighting_unavailable_reason)
        if (!m_atmosphere_bake_resources_supported)
            ZENGINE_CORE_WARN("[SkyEnvironment] Atmosphere baking is disabled: {}", m_atmosphere_bake_unavailable_reason)
        if (!m_atmosphere_view_resources_supported)
            ZENGINE_CORE_WARN("[SkyEnvironment] Per-view atmosphere composition is disabled: {}", m_atmosphere_view_unavailable_reason)
        m_sky_atmosphere_transmittance_pass   = ZPushStructCtorArgs<SkyAtmosphereTransmittancePass>(Device->Arena, &m_sky_environment);
        m_sky_atmosphere_multiscattering_pass = ZPushStructCtorArgs<SkyAtmosphereMultiscatteringPass>(Device->Arena, &m_sky_environment);
        m_sky_atmosphere_source_radiance_pass = ZPushStructCtorArgs<SkyAtmosphereSourceRadiancePass>(Device->Arena, &m_sky_environment);
        m_sky_hdri_mip_generation_pass        = ZPushStructCtorArgs<SkyEnvironmentMipGenerationPass>(Device->Arena, &m_sky_environment, "sky_environment_mip_generation", false);
        m_sky_atmosphere_mip_generation_pass  = ZPushStructCtorArgs<SkyEnvironmentMipGenerationPass>(Device->Arena, &m_sky_environment, "sky_atmosphere_mip_generation", true);
        m_sky_diffuse_irradiance_pass         = ZPushStructCtorArgs<SkyEnvironmentDiffuseIrradiancePass>(Device->Arena, &m_sky_environment);
        m_sky_specular_prefilter_pass         = ZPushStructCtorArgs<SkyEnvironmentSpecularPrefilterPass>(Device->Arena, &m_sky_environment);
        m_lighting_pass->SetEnvironmentLighting(fallback_lighting, m_sky_environment.GetPresentationConfig());
        m_transparent_pass->SetEnvironmentLighting(fallback_lighting, m_sky_environment.GetPresentationConfig());
        m_environment_background_pass->SetEnvironment(fallback_environment, m_sky_environment.GetPresentationConfig());
        RenderGraph->ImportBuffer(RendererBufferName::GlobalVertex, rrm->GetGlobalVertexBuffer());
        RenderGraph->ImportBuffer(RendererBufferName::GlobalIndex, rrm->GetGlobalIndexBuffer());
        m_asset_preview_pass = ZPushStructCtor<AssetPreviewPass>(Device->Arena);
        m_asset_preview_pass->Initialize(Device);
        RenderGraph->AddCallbackPass("Asset Preview", m_asset_preview_pass);
        RenderGraph->AddCallbackPass("Asset Preview Publish", &m_asset_preview_pass->Publish);

        RenderGraph->AddCallbackPass("Frustum Culling Pass", frustum_culling_pass, {.Usage = RGScenePipelineUsage::Core, .Descriptors = RGSceneDescriptorProfile::Culling});
        RenderGraph->AddCallbackPass("Sky Atmosphere Transmittance", m_sky_atmosphere_transmittance_pass, {.Usage = RGScenePipelineUsage::Atmosphere});
        RenderGraph->AddCallbackPass("Sky Atmosphere Multiscattering", m_sky_atmosphere_multiscattering_pass, {.Usage = RGScenePipelineUsage::Atmosphere});
        RenderGraph->AddCallbackPass("Sky Atmosphere Source Radiance", m_sky_atmosphere_source_radiance_pass, {.Usage = RGScenePipelineUsage::Atmosphere});
        RenderGraph->AddCallbackPass("Sky HDRI Source Mip Generation", m_sky_hdri_mip_generation_pass, {.Usage = RGScenePipelineUsage::HDRI});
        RenderGraph->AddCallbackPass("Sky Atmosphere Source Mip Generation", m_sky_atmosphere_mip_generation_pass, {.Usage = RGScenePipelineUsage::Atmosphere});
        RenderGraph->AddCallbackPass("Sky Diffuse Irradiance", m_sky_diffuse_irradiance_pass, {.Usage = RGScenePipelineUsage::EnvironmentLighting});
        RenderGraph->AddCallbackPass("Sky Specular Prefilter", m_sky_specular_prefilter_pass, {.Usage = RGScenePipelineUsage::EnvironmentLighting});
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass, {.Usage = RGScenePipelineUsage::Core, .Formats = {ImageFormat::DEPTH_STENCIL_FROM_DEVICE}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::Geometry, .Targets = SceneTargetMask(SceneRenderTarget::Depth), .AttachmentTargets = {{SceneRenderTarget::Depth, LoadOperation::CLEAR}}});
        RenderGraph->AddCallbackPass(
            "G-Buffer Pass",
            gbuffer_pass,
            {
            .Usage             = RGScenePipelineUsage::Core,
            .Formats           = {               ImageFormat::DEPTH_STENCIL_FROM_DEVICE,                         ImageFormat::R8G8B8A8_UNORM,                           ImageFormat::R16G16B16A16_SFLOAT,                                 ImageFormat::R8G8B8A8_UNORM},
            .AttachmentCount   = 4,
            .Descriptors       = RGSceneDescriptorProfile::Geometry,
            .Targets           = SceneTargetMask(SceneRenderTarget::AlbedoAO) | SceneTargetMask(SceneRenderTarget::NormalRoughness) | SceneTargetMask(SceneRenderTarget::MetallicEmissive),
            .AttachmentTargets = {{SceneRenderTarget::Depth, LoadOperation::LOAD, true}, {SceneRenderTarget::AlbedoAO, LoadOperation::CLEAR}, {SceneRenderTarget::NormalRoughness, LoadOperation::CLEAR}, {SceneRenderTarget::MetallicEmissive, LoadOperation::CLEAR}}
        });
        RenderGraph->AddCallbackPass(
            "Object ID Pass",
            object_id_pass,
            {
            .Usage = RGScenePipelineUsage::Core, .Formats = {                ImageFormat::DEPTH_STENCIL_FROM_DEVICE,                               ImageFormat::R32_UINT},
                 .AttachmentCount = 2, .Descriptors = RGSceneDescriptorProfile::Geometry, .Targets = SceneTargetMask(SceneRenderTarget::ObjectDepth) | SceneTargetMask(SceneRenderTarget::ObjectId), .AttachmentTargets = {{SceneRenderTarget::ObjectDepth, LoadOperation::CLEAR}, {SceneRenderTarget::ObjectId, LoadOperation::CLEAR}}
        });
        RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass, {.Usage = RGScenePipelineUsage::Core, .Formats = {ImageFormat::R16G16B16A16_SFLOAT}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::Lighting, .Targets = SceneTargetMask(SceneRenderTarget::HdrColor), .AttachmentTargets = {{SceneRenderTarget::HdrColor, LoadOperation::CLEAR}}});
        RenderGraph->AddCallbackPass(
            "Sky Sphere Pass",
            sky_sphere_pass,
            {
            .Usage = RGScenePipelineUsage::Sphere, .Formats = {               ImageFormat::DEPTH_STENCIL_FROM_DEVICE, ImageFormat::R16G16B16A16_SFLOAT},
                 .AttachmentCount = 2, .Descriptors = RGSceneDescriptorProfile::Camera, .AttachmentTargets = {{SceneRenderTarget::Depth, LoadOperation::LOAD, true},      SceneRenderTarget::HdrColor}
        });
        RenderGraph->AddCallbackPass(
            "Environment Background Pass",
            environment_background_pass,
            {
            .Usage = RGScenePipelineUsage::Core, .Formats = {               ImageFormat::DEPTH_STENCIL_FROM_DEVICE, ImageFormat::R16G16B16A16_SFLOAT},
                 .AttachmentCount = 2, .Descriptors = RGSceneDescriptorProfile::EnvironmentBackground, .AttachmentTargets = {{SceneRenderTarget::Depth, LoadOperation::LOAD, true},      SceneRenderTarget::HdrColor}
        });
        RenderGraph->AddCallbackPass("Sky View LUT Pass", sky_view_lut_pass, {.Usage = RGScenePipelineUsage::AtmosphereView, .Descriptors = RGSceneDescriptorProfile::SkyViewLut, .Targets = SceneTargetMask(SceneRenderTarget::SkyViewLut)});
        RenderGraph->AddCallbackPass("Aerial Perspective Pass", aerial_pass, {.Usage = RGScenePipelineUsage::AtmosphereView, .Descriptors = RGSceneDescriptorProfile::AerialPerspective, .Targets = SceneTargetMask(SceneRenderTarget::AerialInscatter) | SceneTargetMask(SceneRenderTarget::AerialTransmittance)});
        RenderGraph->AddCallbackPass("Sky Composite Pass", sky_composite_pass, {.Usage = RGScenePipelineUsage::AtmosphereView, .Formats = {ImageFormat::R16G16B16A16_SFLOAT}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::SkyComposite, .Targets = SceneTargetMask(SceneRenderTarget::HdrComposited), .AttachmentTargets = {{SceneRenderTarget::HdrComposited, LoadOperation::CLEAR}}});
        RenderGraph->AddCallbackPass(
            "Transparent Pass",
            m_transparent_pass,
            {
            .Usage = RGScenePipelineUsage::Core, .Formats = {               ImageFormat::DEPTH_STENCIL_FROM_DEVICE, ImageFormat::R16G16B16A16_SFLOAT},
                 .AttachmentCount = 2, .Descriptors = RGSceneDescriptorProfile::Transparent, .AttachmentTargets = {{SceneRenderTarget::Depth, LoadOperation::LOAD, true},      SceneRenderTarget::HdrColor},
                 .CompositedHdrAttachment = true
        });
        RenderGraph->AddCallbackPass("Tone Mapping Pass", tone_mapping_pass, {.Usage = RGScenePipelineUsage::Core, .Formats = {ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::ToneMapping, .Targets = SceneTargetMask(SceneRenderTarget::Color), .AttachmentTargets = {{SceneRenderTarget::Color, LoadOperation::CLEAR}}});
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass, {.Usage = RGScenePipelineUsage::Core, .Formats = {ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::Grid, .AttachmentTargets = {SceneRenderTarget::Color}});
        RenderGraph->AddCallbackPass("Grid Labels Pass", grid_label_pass, {.Usage = RGScenePipelineUsage::Core, .Formats = {ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::GridLabels, .AttachmentTargets = {SceneRenderTarget::Color}});
        RenderGraph->AddCallbackPass("Selection Outline Pass", selection_outline_pass, {.Usage = RGScenePipelineUsage::Core, .Formats = {ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::SelectionOutline, .AttachmentTargets = {SceneRenderTarget::Color}});
        RenderGraph->AddCallbackPass("Gizmo Pass", gizmo_pass, {.Usage = RGScenePipelineUsage::Core, .Formats = {ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = 1, .Descriptors = RGSceneDescriptorProfile::Camera, .AttachmentTargets = {SceneRenderTarget::Color}});
        RenderGraph->Setup();
        RenderGraph->Compile();
        PublishMemoryStatistics();

        // No viewport texture is published here: before the ZUI pass is attached,
        // graph culling deliberately leaves FrameColor unallocated. DrawScene()
        // publishes the first real allocation after an acquired frame compiles it.
    }

    void GraphicRenderer::ReleaseSceneResourceBindingsAfterReadersJoined()
    {
        if (RenderSceneData)
        {
            RenderSceneData->ActiveCulledIndirectBuffer = nullptr;
        }
        ReleaseSceneEnvironmentPreparation();
        if (RenderGraph)
        {
            RenderGraph->ReleaseDescriptorStorageAfterReadersJoined();
        }
    }

    void GraphicRenderer::Deinitialize()
    {
        m_frame_output_sequence.value.fetch_add(1, std::memory_order_acq_rel);
        m_frame_output_index.value.store(UINT64_MAX, std::memory_order_relaxed);
        m_frame_output_generation.value.store(0, std::memory_order_relaxed);
        m_frame_output_sequence.value.fetch_add(1, std::memory_order_release);
        DiscardSkyResources(m_sky_environment.Shutdown());
        EndScenePipelinePreparation();
        m_sky_frame_acquired = false;
        m_scene_sky_lease.Reset();
        m_scene_sky_id                                        = {};
        Scenes::SkyEnvironmentResources retired_sky_resources = {};
        while (m_sky_environment.TakeRetiredSnapshot(UINT64_MAX, retired_sky_resources))
            DiscardSkyResources(retired_sky_resources);
        m_lighting_pass                            = nullptr;
        m_object_id_pass                           = nullptr;
        m_transparent_pass                         = nullptr;
        m_grid_pass                                = nullptr;
        m_gizmo_pass                               = nullptr;
        m_environment_background_pass              = nullptr;
        m_sky_sphere_pass                          = nullptr;
        m_sky_view_lut_pass                        = nullptr;
        m_aerial_perspective_pass                  = nullptr;
        m_sky_composite_pass                       = nullptr;
        m_tone_mapping_pass                        = nullptr;
        m_selection_outline_pass                   = nullptr;
        m_sky_atmosphere_transmittance_pass        = nullptr;
        m_sky_atmosphere_multiscattering_pass      = nullptr;
        m_sky_atmosphere_source_radiance_pass      = nullptr;
        m_sky_hdri_mip_generation_pass             = nullptr;
        m_sky_atmosphere_mip_generation_pass       = nullptr;
        m_sky_diffuse_irradiance_pass              = nullptr;
        m_sky_specular_prefilter_pass              = nullptr;
        m_environment_lighting_resources_supported = false;
        m_atmosphere_bake_resources_supported      = false;
        m_atmosphere_view_resources_supported      = false;

        RenderGraph->Dispose();
        m_scene_pipeline_preparation = nullptr;
        if (RenderSceneData)
        {
            for (uint32_t i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
            {
                Device->GpuMem.FreeBuffer(RenderSceneData->TransformBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->RenderDataBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->MaterialBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->LightBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->SelectionTokenStateBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->CullingInputBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->CulledIndirectBuffers[i]);
            }
        }
    }

    void GraphicRenderer::RequestObjectIdPick(const ObjectIdPickRequest& request)
    {
        if (m_object_id_pass)
        {
            m_object_id_pass->SetObjectIdPickRequest(request);
        }
    }

    void GraphicRenderer::ApplySelectionOutline(bool enabled)
    {
        if (m_selection_outline_pass)
        {
            m_selection_outline_pass->SetEnabled(enabled);
        }
    }

    Hardwares::CommandBuffer* GraphicRenderer::DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, const Cameras::CameraFrameData& camera, const Scenes::PreparedSceneFrame* initial_frame)
    {
        ZENGINE_VALIDATE_ASSERT(frame_index < Scenes::SceneData::MAX_FRAMES_IN_FLIGHT, "Invalid scene-buffer frame index")
        if (initial_frame && initial_frame->View.Captured && !initial_frame->CameraBuffer)
        {
            return nullptr;
        }
        const auto* transforms                      = initial_frame && initial_frame->TransformBuffer ? initial_frame->TransformBuffer : &RenderSceneData->TransformBuffers[frame_index];
        const auto* lights                          = initial_frame && initial_frame->LightBuffer ? initial_frame->LightBuffer : &RenderSceneData->LightBuffers[frame_index];
        const auto* materials                       = initial_frame && initial_frame->MaterialBuffer ? initial_frame->MaterialBuffer : &RenderSceneData->MaterialBuffers[frame_index];
        const auto* draws                           = initial_frame && initial_frame->DrawBuffer ? initial_frame->DrawBuffer : &RenderSceneData->RenderDataBuffers[frame_index];
        const auto* culling                         = initial_frame && frame_index < initial_frame->CullingInputBuffers.size() ? &initial_frame->CullingInputBuffers[frame_index] : &RenderSceneData->CullingInputBuffers[frame_index];
        const auto* indirect                        = initial_frame && frame_index < initial_frame->CulledIndirectBuffers.size() ? &initial_frame->CulledIndirectBuffers[frame_index] : &RenderSceneData->CulledIndirectBuffers[frame_index];
        const auto* selection                       = initial_frame && frame_index < initial_frame->SelectionTokenBuffers.size() ? &initial_frame->SelectionTokenBuffers[frame_index] : &RenderSceneData->SelectionTokenStateBuffers[frame_index];
        RenderSceneData->ActiveCulledIndirectBuffer = indirect;
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Transform, transforms), "Transform buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::RenderData, draws), "Render data buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::SelectionTokenState, selection), "Selection token state buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Material, materials), "Material buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Light, lights), "Light buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::CullingInput, culling), "Culling input buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::CulledIndirect, indirect), "Culled indirect buffer is not imported into the render graph")

        if (Device->RRM && !(initial_frame && initial_frame->MaterialBuffer) && RenderSceneData->MaterialBuffers[frame_index].Handle)
        {
            auto* rrm = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            rrm->UploadFrameMaterials(RenderSceneData->MaterialBuffers[frame_index]);
        }

        // Light buffer is uploaded by AppRenderPipeline::RenderScene from the retained frame snapshot.

        RenderSceneData->ActiveCameraBuffer = initial_frame ? initial_frame->CameraBuffer : nullptr;
        RenderSceneData->CameraHeapOffset   = 0;
        if (!RenderSceneData->ActiveCameraBuffer)
        {
            const auto view_proj              = camera.Projection * camera.View;
            const auto ubo_camera_data        = UBOCameraLayout{.View = camera.View, .Projection = camera.Projection, .Position = Vec4f(camera.Position, 1.0f), .InvViewProj = view_proj.Inverse()};
            auto&      heap                   = Device->FrameHeaps[Device->SwapchainPtr->CurrentFrame->Index];
            const auto camera_alloc           = heap.Push(&ubo_camera_data, sizeof(UBOCameraLayout), Device->MinUniformBufferOffsetAlignment());
            RenderSceneData->CameraHeapOffset = camera_alloc.Offset;
        }

        m_asset_preview_pass->BeginFrame();
        Hardwares::CommandBuffer* const output = RenderGraph->Execute(cb);
        RenderSceneData->ActiveCameraBuffer    = nullptr;
        m_asset_preview_pass->EndFrame();
        FinishSkyFrameRecording(output != nullptr);
        if (output)
        {
            PublishFrameOutput(RenderGraph->ResourceInspector->GetRenderTarget(RendererResourceName::FrameColorRenderTargetName));
        }
        PublishMemoryStatistics();
        return output;
    }

    RendererMemoryStatistics GraphicRenderer::GetMemoryStatistics() const
    {
        while (true)
        {
            const uint64_t sequence_before = m_memory_statistics_sequence.value.load(std::memory_order_acquire);
            if ((sequence_before & 1u) != 0)
                continue;

            RendererMemoryStatistics result = {
                .Vma =
                    {
                          .AllocationBytes           = m_vma_allocation_bytes.value.load(std::memory_order_relaxed),
                          .BlockBytes                = m_vma_block_bytes.value.load(std::memory_order_relaxed),
                          .HeapUsageBytes            = m_vma_heap_usage_bytes.value.load(std::memory_order_relaxed),
                          .HeapBudgetBytes           = m_vma_heap_budget_bytes.value.load(std::memory_order_relaxed),
                          .HeapCount                 = static_cast<uint32_t>(m_vma_heap_count.value.load(std::memory_order_relaxed)),
                          .UsesDriverBudgetTelemetry = m_vma_uses_driver_budget.value.load(std::memory_order_relaxed) != 0,
                          },
                .EnvironmentReservedBytes     = m_environment_reserved_bytes.value.load(std::memory_order_relaxed),
                .EnvironmentBudgetBytes       = m_environment_budget_bytes.value.load(std::memory_order_relaxed),
                .TransientVirtualImageBytes   = m_transient_virtual_image_bytes.value.load(std::memory_order_relaxed),
                .TransientPhysicalImageBytes  = m_transient_physical_image_bytes.value.load(std::memory_order_relaxed),
                .TransientVirtualBufferBytes  = m_transient_virtual_buffer_bytes.value.load(std::memory_order_relaxed),
                .TransientPhysicalBufferBytes = m_transient_physical_buffer_bytes.value.load(std::memory_order_relaxed),
                .TransientImageBackingCount   = static_cast<uint32_t>(m_transient_image_backing_count.value.load(std::memory_order_relaxed)),
                .TransientBufferBackingCount  = static_cast<uint32_t>(m_transient_buffer_backing_count.value.load(std::memory_order_relaxed)),
            };

            const uint64_t sequence_after = m_memory_statistics_sequence.value.load(std::memory_order_acquire);
            if (sequence_before == sequence_after)
                return result;
        }
    }

    Textures::TextureHandle GraphicRenderer::GetFrameOutput()
    {
        while (true)
        {
            const uint64_t sequence_before = m_frame_output_sequence.value.load(std::memory_order_acquire);
            if ((sequence_before & 1u) != 0)
                continue;

            const Textures::TextureHandle output = {
                .Index      = m_frame_output_index.value.load(std::memory_order_relaxed),
                .Generation = m_frame_output_generation.value.load(std::memory_order_relaxed),
            };

            const uint64_t sequence_after = m_frame_output_sequence.value.load(std::memory_order_acquire);
            if (sequence_before == sequence_after)
                return output;
        }
    }

    void GraphicRenderer::PublishFrameOutput(Textures::TextureHandle output)
    {
        if (!output.Valid())
            return;

        const uint64_t previous_index      = m_frame_output_index.value.load(std::memory_order_relaxed);
        const uint64_t previous_generation = m_frame_output_generation.value.load(std::memory_order_relaxed);
        if (previous_index == output.Index && previous_generation == output.Generation)
            return;

        m_frame_output_sequence.value.fetch_add(1, std::memory_order_acq_rel);
        m_frame_output_index.value.store(output.Index, std::memory_order_relaxed);
        m_frame_output_generation.value.store(output.Generation, std::memory_order_relaxed);
        m_frame_output_sequence.value.fetch_add(1, std::memory_order_release);

        // A descriptor write is required when this slot first becomes the
        // viewport output. Rewriting the same slot every frame can modify a
        // descriptor set still in use by the previous GPU submission.
        // RenderGraph::Resize queues its own write after reconstructing the
        // backing image while the handle remains stable.
        Device->RequestDescriptorUpdate(output);
    }

    void GraphicRenderer::PublishMemoryStatistics()
    {
        if (!Device || !RenderGraph)
            return;

        const Core::Memory::GpuMemoryStatistics vma        = Device->GpuMem.GetMemoryStatistics();
        const RGTransientStatistics&            transients = RenderGraph->GetTransientStatistics();

        m_memory_statistics_sequence.value.fetch_add(1, std::memory_order_acq_rel);
        m_vma_allocation_bytes.value.store(vma.AllocationBytes, std::memory_order_relaxed);
        m_vma_block_bytes.value.store(vma.BlockBytes, std::memory_order_relaxed);
        m_vma_heap_usage_bytes.value.store(vma.HeapUsageBytes, std::memory_order_relaxed);
        m_vma_heap_budget_bytes.value.store(vma.HeapBudgetBytes, std::memory_order_relaxed);
        m_vma_heap_count.value.store(vma.HeapCount, std::memory_order_relaxed);
        m_vma_uses_driver_budget.value.store(vma.UsesDriverBudgetTelemetry ? 1u : 0u, std::memory_order_relaxed);
        m_environment_reserved_bytes.value.store(m_sky_environment.GetReservedMemoryBytes(), std::memory_order_relaxed);
        m_environment_budget_bytes.value.store(m_sky_environment.GetMemoryBudgetBytes(), std::memory_order_relaxed);
        m_transient_virtual_image_bytes.value.store(transients.VirtualImageBytes, std::memory_order_relaxed);
        m_transient_physical_image_bytes.value.store(transients.PhysicalImageBytes, std::memory_order_relaxed);
        m_transient_virtual_buffer_bytes.value.store(transients.VirtualBufferBytes, std::memory_order_relaxed);
        m_transient_physical_buffer_bytes.value.store(transients.PhysicalBufferBytes, std::memory_order_relaxed);
        m_transient_image_backing_count.value.store(transients.ImageBackingAllocationCount, std::memory_order_relaxed);
        m_transient_buffer_backing_count.value.store(transients.BufferBackingAllocationCount, std::memory_order_relaxed);
        m_memory_statistics_sequence.value.fetch_add(1, std::memory_order_release);
    }

    bool GraphicRenderer::SubmitSkySnapshot(const Scenes::SkyConfig& sky, const Scenes::SkyCelestialLight& celestial_light, uint64_t revision, Scenes::SkyEnvironmentPublication publication)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkyRequest");
        const EnvironmentLightingBakeSettings bake_settings       = Device ? Device->EnvironmentLightingBakeSettings : ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier::Standard);
        uint64_t                              hdri_source_hash    = 0;
        bool                                  hdri_artifact_ready = !sky.IsHDRI();
        if (sky.IsHDRI())
        {
            if (auto* const asset_manager = ZEngine::Managers::AssetManager::Instance(); asset_manager && asset_manager->Registry)
            {
                std::lock_guard lock(asset_manager->IngestMutex);
                if (const auto* const environment = asset_manager->Registry->FindByUUID(sky.EnvironmentMap))
                {
                    hdri_source_hash    = environment->Meta.SourceHash;
                    hdri_artifact_ready = environment->State == Core::VFS::AssetState::Loaded && environment->Meta.ArtifactPath[0] != '\0';
                }
            }
        }

        return m_sky_environment.SubmitConfig(sky, revision, bake_settings, celestial_light, hdri_source_hash, hdri_artifact_ready, publication);
    }

    void GraphicRenderer::ReleaseSceneEnvironmentPreparation()
    {
        EndScenePipelinePreparation();
        m_scene_sky_lease.Reset();
        m_scene_sky_id                  = {};
        m_scene_sky_revision            = 0;
        m_scene_sky_cancelled           = false;
        // The environment observed a newer candidate revision, even when the
        // outgoing scene's authoring revision has not changed in the meantime.
        m_presented_render_sky_revision = 0;
    }

    void GraphicRenderer::EndScenePipelinePreparation()
    {
        if (RenderGraph)
        {
            RenderGraph->SetSceneTextureSources({});
        }
        for (auto& source : m_scene_texture_sources)
        {
            source.Reset();
        }
        m_scene_texture_sources_ready = false;
        m_scene_grid_font_source.Reset();
        if (m_scene_pipeline_preparation && m_scene_pipeline_active)
        {
            m_scene_pipeline_preparation->EndScenePipelinePreparation();
        }
        m_scene_pipeline_state      = RGScenePipelinePreparationState::Idle;
        m_scene_pipeline_generation = 0;
        m_scene_pipeline_active     = false;
    }

    GraphicRenderer::SceneTextureSourceState GraphicRenderer::PrepareSceneGridLabelFont(const Scenes::PreparedSceneFrame& frame, bool* enabled)
    {
        *enabled                  = false;
        auto grid                 = frame.Grid;
        grid.Enabled              = grid.Enabled && frame.View.ShowGrid;
        GridLabelFrameData labels = {};
        if (!BuildGridLabelFrame(grid, frame.View.Camera, frame.View.GridLabelFont, frame.View.Width, frame.View.Height, labels))
        {
            return SceneTextureSourceState::Failed;
        }
        if (!labels.Count)
        {
            return SceneTextureSourceState::Ready;
        }
        auto*       resources = Device && Device->RRM ? static_cast<RenderResourceManager*>(Device->RRM) : nullptr;
        const auto* texture   = resources ? resources->GetTexture(labels.Atlas) : nullptr;
        const auto* image     = texture ? Device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
        if (!image || !image->GetBuffer().OwnsAllocation || texture->Specification.IsAliasable || labels.Atlas.Index >= RenderResourceManager::TextureVersionCapacity || resources->GetTextureDecodeState(labels.Atlas) == RenderResourceManager::TextureDecodeState::Failed)
        {
            return SceneTextureSourceState::Failed;
        }
        if (!m_scene_grid_font_source)
        {
            m_scene_grid_font_source = resources->PinTextureVersion(labels.Atlas);
            if (!m_scene_grid_font_source)
            {
                return SceneTextureSourceState::Pending;
            }
        }
        const auto retained = m_scene_grid_font_source.Get()->Handle;
        if (retained.Index != labels.Atlas.Index || retained.Generation != labels.Atlas.Generation)
        {
            return SceneTextureSourceState::Failed;
        }
        *enabled = true;
        return SceneTextureSourceState::Ready;
    }

    GraphicRenderer::SceneTextureSourceState GraphicRenderer::PrepareSceneTextureSources(const Scenes::SkyEnvironmentSnapshot& snapshot, const Scenes::SkyConfig& config)
    {
        if (m_scene_texture_sources_ready)
        {
            return SceneTextureSourceState::Ready;
        }
        auto* resources = Device && Device->RRM ? static_cast<RenderResourceManager*>(Device->RRM) : nullptr;
        if (!resources)
        {
            return SceneTextureSourceState::Failed;
        }
        // SkySphere intentionally borrows the permanent fallback snapshot; its
        // snapshot config is not the candidate's presentation/bake contract.
        const auto& lighting = config.IsSkySphere() ? m_sky_environment.GetFallbackLighting() : snapshot.Lighting;
        if (!snapshot.SourceRadiance.Valid() || !lighting.Valid() || (config.IsAtmosphere() && !snapshot.Atmosphere.Valid()))
        {
            return SceneTextureSourceState::Failed;
        }
        const Textures::TextureHandle                           handles[] = {snapshot.SourceRadiance, snapshot.Atmosphere.Transmittance, snapshot.Atmosphere.Multiscattering, lighting.DiffuseIrradiance, lighting.SpecularEnvironment, lighting.BrdfIntegrationLut};
        std::array<RenderResourceManager::TextureVersionPin, 6> sources   = {};
        for (const auto handle : handles)
        {
            if (!handle.Valid())
            {
                continue;
            }
            const auto* texture = resources->GetTexture(handle);
            const auto* image   = texture ? Device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
            if (!image || handle.Index >= RenderResourceManager::TextureVersionCapacity || !image->GetBuffer().OwnsAllocation || texture->Specification.IsAliasable || resources->GetTextureDecodeState(handle) == RenderResourceManager::TextureDecodeState::Failed)
            {
                return SceneTextureSourceState::Failed;
            }
        }
        for (uint32_t index = 0; index < sources.size(); ++index)
        {
            if (!handles[index].Valid())
            {
                continue;
            }
            sources[index] = resources->PinTextureVersion(handles[index]);
            if (!sources[index])
            {
                // A temporary upload/admission gate preserves the outgoing
                // scene. Never publish a partially retained candidate bank.
                return SceneTextureSourceState::Pending;
            }
        }
        m_scene_texture_sources       = std::move(sources);
        m_scene_texture_sources_ready = true;
        return SceneTextureSourceState::Ready;
    }

    void GraphicRenderer::PollSceneEnvironmentPreparations()
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.EnvironmentPreparation");
        if (!Device || !Device->RRM)
        {
            return;
        }
        auto* const rrm = static_cast<RenderResourceManager*>(Device->RRM);
        PollSkyBake();
        CollectRetiredSkySnapshots();
        StartPendingSkyBake();
        if (!m_scene_sky_lease)
        {
            // Activation is not recording. Keep the shader transaction pinned
            // through failed/minimized frames before admitting another candidate.
            if (m_scene_pipeline_generation != 0)
            {
                return;
            }
            RenderResourceManager::SceneEnvironmentPreparation preparation = {};
            m_scene_sky_lease                                              = rrm->ClaimSceneEnvironmentPreparation(&preparation);
            if (!m_scene_sky_lease)
            {
                return;
            }
            m_scene_sky_id = preparation.Id;
            if (m_sky_revision_sequence == UINT64_MAX)
            {
                rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false);
                ReleaseSceneEnvironmentPreparation();
                return;
            }
            m_scene_sky_revision = ++m_sky_revision_sequence;
            auto usage           = RGScenePipelineUsage::Core;
            if (preparation.Input.Sky.IsSkySphere())
            {
                usage = usage | RGScenePipelineUsage::Sphere;
            }
            else if (preparation.Input.Sky.IsHDRI())
            {
                usage = usage | RGScenePipelineUsage::HDRI | RGScenePipelineUsage::EnvironmentLighting;
            }
            else if (preparation.Input.Sky.IsAtmosphere())
            {
                usage = usage | RGScenePipelineUsage::Atmosphere | RGScenePipelineUsage::EnvironmentLighting;
                if (m_atmosphere_view_resources_supported)
                {
                    usage = usage | RGScenePipelineUsage::AtmosphereView;
                }
            }
            if (!m_scene_pipeline_preparation || !m_scene_pipeline_preparation->BeginScenePipelinePreparation(usage))
            {
                rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false, RenderResourceManager::SceneResourcePreparationError::RequiredPipeline);
                ReleaseSceneEnvironmentPreparation();
                return;
            }
            m_scene_pipeline_state  = RGScenePipelinePreparationState::Preparing;
            m_scene_pipeline_active = true;
            if (!SubmitSkySnapshot(preparation.Input.Sky, preparation.Input.CelestialLight, m_scene_sky_revision, Scenes::SkyEnvironmentPublication::Explicit))
            {
                rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false);
                ReleaseSceneEnvironmentPreparation();
                return;
            }
            StartPendingSkyBake();
        }
        RenderResourceManager::SceneEnvironmentPreparation preparation = {};
        if (!rrm->GetSceneEnvironmentPreparation(m_scene_sky_id, &preparation) || preparation.Cancelled)
        {
            if (!m_scene_sky_cancelled)
            {
                m_sky_environment.CancelPrepared(m_scene_sky_revision);
                m_scene_sky_cancelled = true;
            }
            PollSkyBake();
            if (!m_sky_environment.GetActiveBake())
            {
                ReleaseSceneEnvironmentPreparation();
            }
            return;
        }
        if (preparation.State == RenderResourceManager::SceneEnvironmentPreparationState::Preparing)
        {
            m_scene_pipeline_state = m_scene_pipeline_preparation->PollScenePipelinePreparation();
            if (m_scene_pipeline_state == RGScenePipelinePreparationState::Failed || m_scene_pipeline_state == RGScenePipelinePreparationState::Idle)
            {
                rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false, RenderResourceManager::SceneResourcePreparationError::RequiredPipeline);
                m_sky_environment.CancelPrepared(m_scene_sky_revision);
                ReleaseSceneEnvironmentPreparation();
                return;
            }
            StartPendingSkyBake();
            const auto* prepared_snapshot = m_sky_environment.GetPreparedSnapshot(m_scene_sky_revision);
            const auto  texture_state     = m_scene_pipeline_state == RGScenePipelinePreparationState::Ready && prepared_snapshot ? PrepareSceneTextureSources(*prepared_snapshot, preparation.Input.Sky) : SceneTextureSourceState::Pending;
            if (texture_state == SceneTextureSourceState::Failed)
            {
                rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false, RenderResourceManager::SceneResourcePreparationError::RequiredDescriptor);
                m_sky_environment.CancelPrepared(m_scene_sky_revision);
                ReleaseSceneEnvironmentPreparation();
                return;
            }
            if (texture_state == SceneTextureSourceState::Ready)
            {
                Scenes::PreparedSceneFrame frame = {};
                if (rrm->GetPreparedSceneFrame(m_scene_sky_id, &frame))
                {
                    bool       grid_labels_enabled = false;
                    const auto font_state          = PrepareSceneGridLabelFont(frame, &grid_labels_enabled);
                    if (font_state == SceneTextureSourceState::Pending)
                    {
                        return;
                    }
                    if (font_state == SceneTextureSourceState::Failed)
                    {
                        rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false, RenderResourceManager::SceneResourcePreparationError::RequiredDescriptor);
                        m_sky_environment.CancelPrepared(m_scene_sky_revision);
                        ReleaseSceneEnvironmentPreparation();
                        return;
                    }
                    const auto                       atmosphere     = Scenes::MakeAtmosphereViewSettings(prepared_snapshot->Config.Atmosphere, preparation.Input.Sky.Atmosphere);
                    const auto                       view_class     = Scenes::ClassifyAtmosphereView(atmosphere, frame.View.Camera.Position);
                    const bool                       use_composited = m_atmosphere_view_resources_supported && prepared_snapshot->Config.IsAtmosphere() && prepared_snapshot->Atmosphere.Valid() && prepared_snapshot->CelestialLight.IsAvailable && prepared_snapshot->CelestialLight.IsValid() && (view_class == Scenes::AtmosphereViewClass::InsideAtmosphere || view_class == Scenes::AtmosphereViewClass::OutsideAtmosphere);
                    const SceneDescriptorEnvironment environment    = {.SourceRadiance = &m_scene_texture_sources[0], .DiffuseIrradiance = &m_scene_texture_sources[3], .SpecularEnvironment = &m_scene_texture_sources[4], .BrdfIntegrationLut = &m_scene_texture_sources[5], .AtmosphereTransmittance = &m_scene_texture_sources[1], .AtmosphereMultiscattering = &m_scene_texture_sources[2], .UseCompositedSceneColor = use_composited, .GridLabelFont = &m_scene_grid_font_source, .SelectionTokenBuffers = frame.SelectionTokenBuffers, .GridLabelsEnabled = grid_labels_enabled};
                    const auto                       descriptors    = m_scene_pipeline_preparation->PollSceneDescriptorPreparation(frame, 1, environment);
                    if (descriptors == RGScenePipelinePreparationState::Preparing)
                    {
                        return;
                    }
                    if (descriptors != RGScenePipelinePreparationState::Ready)
                    {
                        rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false, RenderResourceManager::SceneResourcePreparationError::RequiredDescriptor);
                        m_sky_environment.CancelPrepared(m_scene_sky_revision);
                        ReleaseSceneEnvironmentPreparation();
                        return;
                    }
                }
                rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, true);
            }
            else if (!m_sky_environment.GetActiveBake() && m_sky_environment.GetState() == Scenes::SkyEnvironmentState::Failed)
            {
                rrm->CompleteSceneEnvironmentPreparation(m_scene_sky_id, false);
                m_sky_environment.CancelPrepared(m_scene_sky_revision);
                ReleaseSceneEnvironmentPreparation();
            }
        }
    }

    bool GraphicRenderer::HasPreparedSceneTargets(RenderResourceManager::SceneResourcePreparationId resources, uint32_t width, uint32_t height) const
    {
        return resources && resources == m_scene_sky_id && m_scene_pipeline_active && RenderGraph && RenderGraph->HasPreparedSceneTargets(width, height);
    }

    bool GraphicRenderer::ApplySkyConfig(const Scenes::SkyConfig& sky, const Scenes::SkyCelestialLight& celestial_light, uint64_t revision, uint64_t scene_generation, RenderResourceManager::SceneResourcePreparationId resources)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.EnvironmentActivation");
        PollSceneEnvironmentPreparations();
        if (resources)
        {
            RenderResourceManager::SceneEnvironmentPreparation preparation = {};
            auto* const                                        rrm         = Device && Device->RRM ? static_cast<RenderResourceManager*>(Device->RRM) : nullptr;
            if (!rrm || !rrm->GetSceneEnvironmentPreparation(resources, &preparation) || preparation.ExpectedGeneration + 1 != scene_generation)
            {
                return false;
            }
            if (preparation.State == RenderResourceManager::SceneEnvironmentPreparationState::Ready)
            {
                if (preparation.Cancelled || resources != m_scene_sky_id || !m_scene_texture_sources_ready || !m_sky_environment.GetPreparedSnapshot(m_scene_sky_revision) || !m_scene_pipeline_preparation->ActivateSceneDescriptors() || !rrm->ActivateSceneEnvironmentPreparation(resources))
                {
                    return false;
                }
                const bool activated = m_sky_environment.ActivatePrepared(m_scene_sky_revision);
                ZENGINE_VALIDATE_ASSERT(activated, "Ready scene environment lost its render-owned snapshot")
                m_presented_scene_generation    = scene_generation;
                m_presented_sky_revision        = preparation.Input.SkyRevision;
                m_presented_render_sky_revision = m_scene_sky_revision;
                m_scene_pipeline_generation     = scene_generation;
                if (RenderGraph)
                {
                    RenderGraph->SetSceneTextureSources(m_scene_texture_sources);
                }
                m_scene_sky_lease.Reset();
                m_scene_sky_id        = {};
                m_scene_sky_revision  = 0;
                m_scene_sky_cancelled = false;
                return true;
            }
            if (preparation.State != RenderResourceManager::SceneEnvironmentPreparationState::NotRequested && preparation.State != RenderResourceManager::SceneEnvironmentPreparationState::Activated)
            {
                return false;
            }
        }
        // Coalesced authoring edits cannot replace the prepared input before
        // its first successful recording, including retries after graph rejection.
        if (m_scene_pipeline_generation != 0)
        {
            return scene_generation == m_scene_pipeline_generation;
        }
        // Outgoing frames continue selecting their retained environment while
        // the candidate owns the scheduler. Do not supersede it with old inputs.
        if (m_scene_sky_lease)
        {
            return scene_generation == m_presented_scene_generation;
        }
        if (revision == 0)
        {
            return true;
        }
        if (m_presented_render_sky_revision == 0 || m_presented_scene_generation != scene_generation || m_presented_sky_revision != revision)
        {
            if (m_sky_revision_sequence == UINT64_MAX)
            {
                return false;
            }
            m_presented_render_sky_revision = ++m_sky_revision_sequence;
            m_presented_scene_generation    = scene_generation;
            m_presented_sky_revision        = revision;
        }
        if (SubmitSkySnapshot(sky, celestial_light, m_presented_render_sky_revision, Scenes::SkyEnvironmentPublication::Immediate))
        {
            StartPendingSkyBake();
        }
        PollSkyBake();
        return true;
    }

    void GraphicRenderer::BeginSkyFrame(const Cameras::CameraFrameData& camera)
    {
        PollSkyBake();
        StartPendingSkyBake();
        CollectRetiredSkySnapshots();

        if (!m_lighting_pass || !m_environment_background_pass || !m_sky_sphere_pass || !m_grid_pass || !m_tone_mapping_pass || !m_sky_view_lut_pass || !m_aerial_perspective_pass || !m_sky_composite_pass)
            return;

        const Scenes::SkyEnvironmentSnapshot* snapshot = m_sky_environment.AcquireForFrame();

        // Reset every optional callback before handling the selected snapshot.
        // This prevents a failed/minimized frame from retaining a prior view's
        // declarations on the next graph registration.
        m_sky_view_lut_pass->SetEnvironment(nullptr, {});
        m_aerial_perspective_pass->SetEnvironment(nullptr, {});
        m_sky_composite_pass->SetEnvironment(nullptr, {});
        m_sky_sphere_pass->SetEnvironment({}, {});
        m_sky_view_lut_pass->SetCameraPosition(camera.Position);
        m_aerial_perspective_pass->SetCameraPosition(camera.Position);
        m_sky_composite_pass->SetCameraPosition(camera.Position);
        m_sky_composite_pass->SetCameraDepthConvention(camera.UsesReverseZ);
        m_sky_sphere_pass->SetCameraDepthConvention(camera.UsesReverseZ);
        m_environment_background_pass->SetUseSolidColorFallback(false);
        m_environment_background_pass->SetCameraDepthConvention(camera.UsesReverseZ);
        m_grid_pass->SetCameraDepthConvention(camera.UsesReverseZ);
        m_tone_mapping_pass->SetUseCompositedSceneColor(false);
        m_transparent_pass->SetUseCompositedSceneColor(false);
        m_environment_background_pass->SetActive(true);
        if (!snapshot)
            return;

        const Scenes::SkyConfig& presentation   = m_sky_environment.GetPresentationConfig();
        const bool               use_sky_sphere = presentation.IsSkySphere();
        m_lighting_pass->SetEnvironmentLighting(use_sky_sphere ? m_sky_environment.GetFallbackLighting() : snapshot->Lighting, presentation);
        m_transparent_pass->SetEnvironmentLighting(use_sky_sphere ? m_sky_environment.GetFallbackLighting() : snapshot->Lighting, presentation);
        m_environment_background_pass->SetEnvironment(snapshot->SourceRadiance, presentation);
        m_sky_sphere_pass->SetEnvironment(use_sky_sphere ? presentation : Scenes::SkyConfig{}, use_sky_sphere ? m_sky_environment.GetPresentationCelestialLight() : Scenes::SkyCelestialLight{});
        const Scenes::AtmosphereSettings  view_atmosphere = Scenes::MakeAtmosphereViewSettings(snapshot->Config.Atmosphere, presentation.Atmosphere);
        const Scenes::AtmosphereViewClass view_class      = Scenes::ClassifyAtmosphereView(view_atmosphere, camera.Position);
        if (snapshot->Config.IsAtmosphere() && view_class != m_last_atmosphere_view_class)
        {
            if (view_class == Scenes::AtmosphereViewClass::BelowGround)
                ZENGINE_CORE_WARN("[SkyEnvironment] Camera entered a below-ground atmosphere view; using the baked sky fallback until it returns above the planet surface")
            else if (view_class == Scenes::AtmosphereViewClass::Invalid)
                ZENGINE_CORE_WARN("[SkyEnvironment] Camera or atmosphere placement is invalid; using the baked sky fallback")
            else
                ZENGINE_CORE_INFO("[SkyEnvironment] Camera atmosphere view is now {}", Scenes::GetAtmosphereViewClassName(view_class))
            m_last_atmosphere_view_class = view_class;
        }

        const bool view_supports_atmosphere = view_class == Scenes::AtmosphereViewClass::InsideAtmosphere || view_class == Scenes::AtmosphereViewClass::OutsideAtmosphere;
        const bool use_atmosphere_view      = m_atmosphere_view_resources_supported && snapshot->Config.IsAtmosphere() && snapshot->Atmosphere.Valid() && snapshot->CelestialLight.IsAvailable && snapshot->CelestialLight.IsValid() && view_supports_atmosphere;
        m_environment_background_pass->SetActive(!use_atmosphere_view && !use_sky_sphere);
        m_environment_background_pass->SetUseSolidColorFallback(snapshot->Config.IsAtmosphere() && (view_class == Scenes::AtmosphereViewClass::BelowGround || view_class == Scenes::AtmosphereViewClass::Invalid));
        m_sky_view_lut_pass->SetEnvironment(use_atmosphere_view ? snapshot : nullptr, presentation);
        m_aerial_perspective_pass->SetEnvironment(use_atmosphere_view ? snapshot : nullptr, presentation);
        m_sky_composite_pass->SetEnvironment(use_atmosphere_view ? snapshot : nullptr, presentation);
        m_tone_mapping_pass->SetUseCompositedSceneColor(use_atmosphere_view);
        m_transparent_pass->SetUseCompositedSceneColor(use_atmosphere_view);
        m_sky_frame_acquired = true;
    }

    void GraphicRenderer::FinishSkyFrameRecording(bool recorded)
    {
        if (recorded && m_scene_pipeline_generation != 0 && m_scene_pipeline_generation == m_presented_scene_generation)
        {
            EndScenePipelinePreparation();
        }
        if (!m_sky_frame_acquired)
        {
            return;
        }
        m_sky_frame_acquired = false;
        if (!recorded)
        {
            m_sky_environment.ReleaseCancelledFrame();
            return;
        }
        Device->SwapchainPtr->EnqueueRenderWorkSubmittedCallback(&GraphicRenderer::OnSkyFrameSubmitted, this, &GraphicRenderer::OnSkyFrameCancelled);
        if (m_sky_environment.CanRecordGpuBakeStage())
        {
            Device->SwapchainPtr->EnqueueRenderWorkSubmittedCallback(&GraphicRenderer::OnSkyBakeStageSubmitted, this);
        }
    }

    void GraphicRenderer::StartPendingSkyBake()
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkyBakeAdmission");
        if (m_scene_sky_lease && m_scene_pipeline_state != RGScenePipelinePreparationState::Ready)
        {
            return;
        }
        Scenes::SkyEnvironmentBakeRequest request = {};
        if (!m_sky_environment.TakeBakeRequest(request))
            return;

        if (request.Config.IsAtmosphere())
        {
            if (!request.BakeInputsValid)
            {
                ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: atmosphere requires valid settings and a selected directional light", request.Revision)
                m_sky_environment.CompleteBake(request.Revision, {}, false);
                return;
            }

            if (!m_environment_lighting_resources_supported)
            {
                ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: atmosphere IBL is unavailable ({})", request.Revision, m_environment_lighting_unavailable_reason)
                m_sky_environment.CompleteBake(request.Revision, {}, false);
                return;
            }

            if (!m_atmosphere_bake_resources_supported)
            {
                ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: atmosphere baking is unavailable ({})", request.Revision, m_atmosphere_bake_unavailable_reason)
                m_sky_environment.CompleteBake(request.Revision, {}, false);
                return;
            }

            const uint64_t atmosphere_bake_bytes = EstimateAtmosphereBakeBytes(request.BakeSettings);
            if (!m_sky_environment.ReserveActiveBakeMemory(request.Revision, atmosphere_bake_bytes))
            {
                ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: atmosphere bake needs {} bytes but {} of the {} byte environment budget is reserved", request.Revision, atmosphere_bake_bytes, m_sky_environment.GetReservedMemoryBytes(), m_sky_environment.GetMemoryBudgetBytes())
                m_sky_environment.CompleteBake(request.Revision, {}, false);
                return;
            }

            const Scenes::AtmosphereStaticResources* reusable_atmosphere = m_sky_environment.FindReusableAtmosphere(request.Config);
            const Scenes::AtmosphereStaticResources  atmosphere          = reusable_atmosphere ? *reusable_atmosphere : CreateAtmosphereStaticResources();
            const bool                               owns_atmosphere     = reusable_atmosphere == nullptr;
            const Textures::TextureHandle            source              = CreateAtmosphereSourceRadiance(request.BakeSettings);
            const EnvironmentLightingResources       lighting            = CreateSkyLightingResources(request.BakeSettings);
            if (!atmosphere.Valid() || !source.Valid() || !lighting.Valid() || !m_sky_environment.AttachBakeAtmosphere(request.Revision, atmosphere, owns_atmosphere) || !m_sky_environment.AttachBakeResource(request.Revision, source) || !m_sky_environment.AttachBakeLighting(request.Revision, lighting) || !m_sky_environment.BeginGpuBake(request.Revision))
            {
                const Scenes::SkyEnvironmentBakeResult result = m_sky_environment.CompleteBake(request.Revision, source, false, lighting, atmosphere);
                DiscardSkyResources({.Atmosphere = owns_atmosphere ? atmosphere : Scenes::AtmosphereStaticResources{}, .SourceRadiance = source, .Lighting = lighting});
                if (result != Scenes::SkyEnvironmentBakeResult::Discarded)
                    ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} could not allocate atmosphere bake targets", request.Revision)
                StartPendingSkyBake();
                return;
            }

            ZENGINE_CORE_INFO("[SkyEnvironment] GPU baking atmosphere revision {}", request.Revision)
            return;
        }

        // HDRI preparation is asynchronous. The last published snapshot remains
        // bound until all three IBL bake stages have completed.
        if (!request.BakeInputsValid || !request.Config.IsHDRI() || request.Config.EnvironmentMap.is_nil())
        {
            ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: the selected sky has no usable HDRI source", request.Revision)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        if (!m_environment_lighting_resources_supported)
        {
            ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: HDRI IBL is unavailable ({})", request.Revision, m_environment_lighting_unavailable_reason)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        auto* const asset_manager = ZEngine::Managers::AssetManager::Instance();
        auto* const rrm           = Device && Device->RRM ? static_cast<Rendering::RenderResourceManager*>(Device->RRM) : nullptr;
        if (!asset_manager || !asset_manager->Registry || !rrm)
        {
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: asset services are unavailable", request.Revision)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        char native_path[MAX_FILE_PATH_COUNT] = {};
        {
            // Registry pointers are borrowed only under the ingest lock. Copy
            // the resolved artifact identity/path before any file I/O or upload.
            std::lock_guard   lock(asset_manager->IngestMutex);
            const auto* const environment = asset_manager->Registry->FindByUUID(request.Config.EnvironmentMap);
            if (!environment)
            {
                ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: HDRI asset is not registered", request.Revision)
                m_sky_environment.CompleteBake(request.Revision, {}, false);
                return;
            }
            if (environment->State != Core::VFS::AssetState::Loaded || environment->Meta.SourceHash != request.HDRISourceHash || environment->Meta.ArtifactPath[0] == '\0')
            {
                ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} cannot use a missing or superseded HDRI artifact", request.Revision)
                m_sky_environment.CompleteBake(request.Revision, {}, false);
                return;
            }

            const auto artifact_path = Core::VFS::VFSPath::Parse(environment->Meta.ArtifactPath);
            if (artifact_path.Failed())
            {
                ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: HDRI cooked-artifact path is invalid", request.Revision)
                m_sky_environment.CompleteBake(request.Revision, {}, false);
                return;
            }
            artifact_path.Value().ResolveNative(asset_manager->CurrentWorkingSpacePath, native_path, sizeof(native_path));
        }
        if (native_path[0] == '\0')
        {
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: HDRI cooked-artifact path cannot be resolved", request.Revision)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        Importers::AssetCodec::EnvironmentMapFileHeader artifact_header = {};
        if (!Importers::AssetCodec::ReadEnvironmentMapFileHeader(native_path, artifact_header) || !Importers::AssetCodec::DoesEnvironmentMapHeaderMatchSource(artifact_header, request.HDRISourceHash))
        {
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: HDRI cooked artifact is stale, corrupt, or incompatible", request.Revision)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        cstring hdri_capability_reason = nullptr;
        if (!SupportsHDRISourceResources(artifact_header.FaceWidth, &hdri_capability_reason))
        {
            ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: HDRI source is unavailable ({})", request.Revision, hdri_capability_reason)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        const uint64_t hdri_bake_bytes = EstimateHDRIBakeBytes(request.BakeSettings, artifact_header.FaceWidth);
        if (!m_sky_environment.ReserveActiveBakeMemory(request.Revision, hdri_bake_bytes))
        {
            ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: HDRI bake needs {} bytes but {} of the {} byte environment budget is reserved", request.Revision, hdri_bake_bytes, m_sky_environment.GetReservedMemoryBytes(), m_sky_environment.GetMemoryBudgetBytes())
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        const Textures::TextureHandle source_radiance = rrm->SubmitTextureFile(native_path, {}, true);
        if (!source_radiance.Valid() || !m_sky_environment.AttachBakeResource(request.Revision, source_radiance))
        {
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: HDRI decode could not be scheduled", request.Revision)
            m_sky_environment.CompleteBake(request.Revision, source_radiance, false);
            return;
        }

        ZENGINE_CORE_INFO("[SkyEnvironment] Baking HDRI revision {}", request.Revision)
    }

    void GraphicRenderer::PollSkyBake()
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkyCompletionPoll");
        const Scenes::SkyEnvironmentBakeRequest* const bake            = m_sky_environment.GetActiveBake();
        const Textures::TextureHandle                  source_radiance = m_sky_environment.GetActiveBakeSource();
        if (!bake || !source_radiance.Valid() || !Device || !Device->RRM)
            return;

        const uint64_t revision = bake->Revision;
        auto* const    rrm      = static_cast<Rendering::RenderResourceManager*>(Device->RRM);

        if (m_sky_environment.GetActiveBakeStage() == Scenes::SkyEnvironmentBakeStage::AwaitingSource)
        {
            const Rendering::RenderResourceManager::TextureDecodeState decode_state = rrm->GetTextureDecodeState(source_radiance);
            if (decode_state == Rendering::RenderResourceManager::TextureDecodeState::Pending)
                return;

            if (decode_state != Rendering::RenderResourceManager::TextureDecodeState::Succeeded)
            {
                rrm->ForgetTextureDecode(source_radiance);
                const Scenes::SkyEnvironmentBakeResult result = m_sky_environment.CompleteBake(revision, source_radiance, false);
                DiscardSkyTexture(source_radiance);
                if (result == Scenes::SkyEnvironmentBakeResult::Discarded)
                    ZENGINE_CORE_INFO("[SkyEnvironment] Cancelled stale HDRI revision {} before GPU baking", revision)
                else
                    ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} failed to decode; retaining the previous ready environment or fallback", revision)
                StartPendingSkyBake();
                return;
            }

            if (!m_sky_environment.IsActiveBakeCurrent())
            {
                rrm->ForgetTextureDecode(source_radiance);
                m_sky_environment.CompleteBake(revision, source_radiance, false);
                DiscardSkyTexture(source_radiance);
                ZENGINE_CORE_INFO("[SkyEnvironment] Cancelled stale HDRI revision {} before GPU baking", revision)
                StartPendingSkyBake();
                return;
            }

            const Hardwares::StreamingUploadTicket* const ticket = rrm->FindStreamingUploadTicket(source_radiance);
            if (!ticket || !ticket->CompletionTimeline)
                return;

            uint64_t completed_upload_value = 0;
            vkGetSemaphoreCounterValue(Device->LogicalDevice, ticket->CompletionTimeline->GetHandle(), &completed_upload_value);
            if (completed_upload_value < ticket->CompletionValue)
                return;

            EnvironmentLightingResources lighting = CreateSkyLightingResources(bake->BakeSettings);
            if (!lighting.Valid() || !m_sky_environment.AttachBakeLighting(revision, lighting) || !m_sky_environment.BeginGpuBake(revision))
            {
                const Scenes::SkyEnvironmentBakeResult result = m_sky_environment.CompleteBake(revision, source_radiance, false);
                DiscardSkyResources({.SourceRadiance = source_radiance, .Lighting = lighting});
                if (result == Scenes::SkyEnvironmentBakeResult::Discarded)
                    ZENGINE_CORE_INFO("[SkyEnvironment] Cancelled stale HDRI revision {} before GPU baking", revision)
                else
                    ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} could not allocate IBL bake targets", revision)
                StartPendingSkyBake();
                return;
            }

            rrm->ForgetTextureDecode(source_radiance);
            ZENGINE_CORE_INFO("[SkyEnvironment] GPU baking HDRI revision {}", revision)
            return;
        }

        if (m_sky_environment.CanRecordGpuBakeStage() || !Device->SwapchainPtr || !Device->SwapchainPtr->RenderTimeline)
            return;

        uint64_t completed_render_value = 0;
        vkGetSemaphoreCounterValue(Device->LogicalDevice, Device->SwapchainPtr->RenderTimeline->GetHandle(), &completed_render_value);
        if (!m_sky_environment.AdvanceCompletedGpuBakeStage(completed_render_value))
            return;

        // The stage boundary above is also a cancellation point. Do not spend
        // more GPU work on a superseded source revision.
        if (!m_sky_environment.IsActiveBakeCurrent())
        {
            const Scenes::AtmosphereStaticResources atmosphere      = m_sky_environment.GetActiveBakeAtmosphere();
            const bool                              owns_atmosphere = m_sky_environment.ActiveBakeOwnsAtmosphere();
            const EnvironmentLightingResources      lighting        = m_sky_environment.GetActiveBakeLighting();
            m_sky_environment.CompleteBake(revision, source_radiance, false);
            DiscardSkyResources({.Atmosphere = owns_atmosphere ? atmosphere : Scenes::AtmosphereStaticResources{}, .SourceRadiance = source_radiance, .Lighting = lighting});
            ZENGINE_CORE_INFO("[SkyEnvironment] Cancelled stale revision {} between GPU bake stages", revision)
            StartPendingSkyBake();
            return;
        }

        if (!m_sky_environment.IsGpuBakeReadyToPublish())
            return;

        const Scenes::AtmosphereStaticResources atmosphere      = m_sky_environment.GetActiveBakeAtmosphere();
        const bool                              owns_atmosphere = m_sky_environment.ActiveBakeOwnsAtmosphere();
        const EnvironmentLightingResources      lighting        = m_sky_environment.GetActiveBakeLighting();
        const Scenes::SkyEnvironmentBakeResult  result          = m_sky_environment.CompleteBake(revision, source_radiance, true, lighting, atmosphere);
        if (result == Scenes::SkyEnvironmentBakeResult::Published)
        {
            ZENGINE_CORE_INFO("[SkyEnvironment] Published sky revision {}", revision)
        }
        else if (result == Scenes::SkyEnvironmentBakeResult::Prepared)
        {
            ZENGINE_CORE_INFO("[SkyEnvironment] Prepared sky revision {} for explicit activation", revision)
        }
        else
        {
            DiscardSkyResources({.Atmosphere = owns_atmosphere ? atmosphere : Scenes::AtmosphereStaticResources{}, .SourceRadiance = source_radiance, .Lighting = lighting});
            if (result == Scenes::SkyEnvironmentBakeResult::Discarded)
                ZENGINE_CORE_INFO("[SkyEnvironment] Discarded stale sky revision {}", revision)
            else
                ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} could not publish; retaining the previous ready environment or fallback", revision)
        }
        StartPendingSkyBake();
    }

    void GraphicRenderer::CollectRetiredSkySnapshots()
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkyRetirement");
        if (!Device || !Device->SwapchainPtr || !Device->SwapchainPtr->RenderTimeline)
            return;

        uint64_t completed_timeline_value = 0;
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkyRetirementTimelinePoll");
            vkGetSemaphoreCounterValue(Device->LogicalDevice, Device->SwapchainPtr->RenderTimeline->GetHandle(), &completed_timeline_value);
        }

        Scenes::SkyEnvironmentResources retired_resources = {};
        while (m_sky_environment.TakeRetiredSnapshot(completed_timeline_value, retired_resources))
        {
            ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkyRetirementDiscard");
            DiscardSkyResources(retired_resources);
        }
    }

    void GraphicRenderer::DiscardSkyTexture(Textures::TextureHandle texture)
    {
        if (!texture.Valid() || !Device)
            return;

        // A stale or retired source is never imported again. If it completed a
        // streamed upload without becoming the published snapshot, consume its
        // ticket before scheduling normal timeline-gated destruction.
        if (Device->RRM)
        {
            auto* const rrm = static_cast<Rendering::RenderResourceManager*>(Device->RRM);
            if (const Hardwares::StreamingUploadTicket* ticket = rrm->FindStreamingUploadTicket(texture))
                rrm->AcknowledgeStreamingUploadTicket(*ticket);
            rrm->ForgetTextureDecode(texture);
        }
        Device->DestroyTexture(texture);
    }

    void GraphicRenderer::DiscardSkyResources(const Scenes::SkyEnvironmentResources& resources)
    {
        DiscardSkyTexture(resources.Atmosphere.Transmittance);
        DiscardSkyTexture(resources.Atmosphere.Multiscattering);
        DiscardSkyTexture(resources.SourceRadiance);
        // The BRDF integration LUT is engine-global and remains owned by RRM.
        // Per-snapshot allocations are only the source, diffuse, and specular cubes.
        DiscardSkyTexture(resources.Lighting.DiffuseIrradiance);
        DiscardSkyTexture(resources.Lighting.SpecularEnvironment);
    }

    bool GraphicRenderer::SupportsEnvironmentLightingResources(const EnvironmentLightingBakeSettings& bake_settings, cstring* out_reason) const
    {
        SetCapabilityReason(out_reason, nullptr);
        if (!Device || Device->PhysicalDevice == VK_NULL_HANDLE)
        {
            SetCapabilityReason(out_reason, "no Vulkan physical device is available");
            return false;
        }
        if (!bake_settings.IsValid())
        {
            SetCapabilityReason(out_reason, "the selected environment-lighting quality tier is invalid");
            return false;
        }

        if (!SupportsSkyFormat(Device->PhysicalDevice, SkyLightingFormat))
        {
            SetCapabilityReason(out_reason, "RGBA16F images cannot be both sampled and written by compute shaders");
            return false;
        }

        const uint32_t cube_resolution = std::max({bake_settings.SourceRadianceResolution, bake_settings.DiffuseResolution, bake_settings.SpecularResolution});
        if (!SupportsSkyImage(Device->PhysicalDevice, SkyLightingFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, cube_resolution, cube_resolution, 1, 6))
        {
            SetCapabilityReason(out_reason, "the selected quality tier exceeds RGBA16F cubemap support");
            return false;
        }
        return true;
    }

    bool GraphicRenderer::SupportsAtmosphereBakeResources(const EnvironmentLightingBakeSettings& bake_settings, cstring* out_reason) const
    {
        if (!SupportsEnvironmentLightingResources(bake_settings, out_reason))
            return false;

        if (!SupportsSkyImage(Device->PhysicalDevice, SkyLightingFormat, VK_IMAGE_TYPE_2D, 0, 256, 64, 1, 1))
        {
            SetCapabilityReason(out_reason, "the device cannot allocate the required RGBA16F atmosphere lookup textures");
            return false;
        }
        return true;
    }

    bool GraphicRenderer::SupportsAtmosphereViewResources(cstring* out_reason) const
    {
        SetCapabilityReason(out_reason, nullptr);
        if (!Device || Device->PhysicalDevice == VK_NULL_HANDLE)
        {
            SetCapabilityReason(out_reason, "no Vulkan physical device is available");
            return false;
        }

        if (!SupportsSkyFormat(Device->PhysicalDevice, SkyLightingFormat))
        {
            SetCapabilityReason(out_reason, "RGBA16F view textures cannot be both sampled and written by compute shaders");
            return false;
        }

        if (!SupportsSkyImage(Device->PhysicalDevice, SkyLightingFormat, VK_IMAGE_TYPE_2D, 0, 192, 108, 1, 1))
        {
            SetCapabilityReason(out_reason, "the device cannot allocate the required RGBA16F sky-view lookup texture");
            return false;
        }

        if (!SupportsSkyImage(Device->PhysicalDevice, SkyLightingFormat, VK_IMAGE_TYPE_3D, 0, 32, 32, 32, 1))
        {
            SetCapabilityReason(out_reason, "the device cannot allocate the required 32³ RGBA16F aerial-perspective volume");
            return false;
        }
        return true;
    }

    bool GraphicRenderer::SupportsHDRISourceResources(uint32_t face_resolution, cstring* out_reason) const
    {
        SetCapabilityReason(out_reason, nullptr);
        if (!Device || Device->PhysicalDevice == VK_NULL_HANDLE)
        {
            SetCapabilityReason(out_reason, "no Vulkan physical device is available");
            return false;
        }
        if (face_resolution == 0 || face_resolution > Importers::AssetCodec::ENVIRONMENT_MAP_MAX_FACE_SIZE)
        {
            SetCapabilityReason(out_reason, "the cooked source has an invalid cubemap face resolution");
            return false;
        }

        if (!SupportsSkyFormat(Device->PhysicalDevice, HDRISourceFormat))
        {
            SetCapabilityReason(out_reason, "RGBA32F cubemaps cannot be both sampled and written by compute shaders");
            return false;
        }

        if (!SupportsSkyImage(Device->PhysicalDevice, HDRISourceFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, face_resolution, face_resolution, 1, 6))
        {
            SetCapabilityReason(out_reason, "the HDRI cubemap exceeds device support");
            return false;
        }
        return true;
    }

    Scenes::AtmosphereStaticResources GraphicRenderer::CreateAtmosphereStaticResources()
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.AtmosphereAllocation");
        if (!Device)
            return {};

        TextureSpecification transmittance_spec     = {};
        transmittance_spec.IsUsageSampled           = true;
        transmittance_spec.IsUsageStorage           = true;
        transmittance_spec.IsUsageTransfert         = false;
        transmittance_spec.Width                    = 256;
        transmittance_spec.Height                   = 64;
        transmittance_spec.BytePerPixel             = sizeof(uint16_t) * 4;
        transmittance_spec.Format                   = ImageFormat::R16G16B16A16_SFLOAT;

        TextureSpecification multiscattering_spec   = transmittance_spec;
        multiscattering_spec.Width                  = 32;
        multiscattering_spec.Height                 = 32;

        Scenes::AtmosphereStaticResources resources = {};
        resources.Transmittance                     = Device->TryCreateTexture(transmittance_spec, "SkyAtmosphereTransmittance");
        resources.Multiscattering                   = Device->TryCreateTexture(multiscattering_spec, "SkyAtmosphereMultiscattering");
        if (!resources.Valid())
        {
            DiscardSkyResources({.Atmosphere = resources});
            return {};
        }
        return resources;
    }

    Textures::TextureHandle GraphicRenderer::CreateAtmosphereSourceRadiance(const EnvironmentLightingBakeSettings& bake_settings)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkySourceAllocation");
        if (!Device || !bake_settings.IsValid())
            return {};

        TextureSpecification source_spec = {};
        source_spec.IsUsageSampled       = true;
        source_spec.IsUsageStorage       = true;
        source_spec.IsUsageTransfert     = false;
        source_spec.IsCubemap            = true;
        source_spec.Width                = bake_settings.SourceRadianceResolution;
        source_spec.Height               = bake_settings.SourceRadianceResolution;
        source_spec.LayerCount           = 6;
        source_spec.MipLevelCount        = GetFullMipCount(bake_settings.SourceRadianceResolution);
        source_spec.BytePerPixel         = sizeof(uint16_t) * 4;
        source_spec.Format               = ImageFormat::R16G16B16A16_SFLOAT;
        return Device->TryCreateTexture(source_spec, "SkyAtmosphereSourceRadiance");
    }

    EnvironmentLightingResources GraphicRenderer::CreateSkyLightingResources(const EnvironmentLightingBakeSettings& bake_settings)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Render.SkyLightingAllocation");
        if (!Device || !Device->RRM || !bake_settings.IsValid())
            return {};

        auto* const rrm      = static_cast<Rendering::RenderResourceManager*>(Device->RRM);
        const auto  fallback = rrm->GetOrCreateFallbackEnvironmentLighting();
        if (!fallback.BrdfIntegrationLut.Valid())
            return {};

        TextureSpecification diffuse_spec      = {};
        diffuse_spec.IsUsageSampled            = true;
        diffuse_spec.IsUsageStorage            = true;
        diffuse_spec.IsUsageTransfert          = false;
        diffuse_spec.IsCubemap                 = true;
        diffuse_spec.Width                     = bake_settings.DiffuseResolution;
        diffuse_spec.Height                    = bake_settings.DiffuseResolution;
        diffuse_spec.LayerCount                = 6;
        diffuse_spec.MipLevelCount             = 1;
        diffuse_spec.BytePerPixel              = sizeof(uint16_t) * 4;
        diffuse_spec.Format                    = ImageFormat::R16G16B16A16_SFLOAT;

        TextureSpecification specular_spec     = diffuse_spec;
        specular_spec.Width                    = bake_settings.SpecularResolution;
        specular_spec.Height                   = bake_settings.SpecularResolution;
        specular_spec.MipLevelCount            = GetFullMipCount(bake_settings.SpecularResolution);

        EnvironmentLightingResources resources = {};
        resources.DiffuseIrradiance            = Device->TryCreateTexture(diffuse_spec, "SkyDiffuseIrradiance");
        resources.SpecularEnvironment          = Device->TryCreateTexture(specular_spec, "SkySpecularEnvironment");
        resources.BrdfIntegrationLut           = fallback.BrdfIntegrationLut;
        resources.BrdfIntegrationKey           = fallback.BrdfIntegrationKey;
        resources.BakeSettings                 = bake_settings;
        resources.SpecularMipCount             = specular_spec.MipLevelCount;
        if (!resources.DiffuseIrradiance.Valid() || !resources.SpecularEnvironment.Valid())
        {
            DiscardSkyResources({.Lighting = resources});
            return {};
        }
        return resources;
    }

    void GraphicRenderer::OnSkyFrameSubmitted(void* context, Rendering::Primitives::Semaphore* /*timeline*/, uint64_t timeline_value)
    {
        if (context)
            static_cast<GraphicRenderer*>(context)->m_sky_environment.ReleaseSubmittedFrame(timeline_value);
    }

    void GraphicRenderer::OnSkyFrameCancelled(void* context)
    {
        if (context)
            static_cast<GraphicRenderer*>(context)->m_sky_environment.ReleaseCancelledFrame();
    }

    void GraphicRenderer::OnSkyBakeStageSubmitted(void* context, Rendering::Primitives::Semaphore* /*timeline*/, uint64_t timeline_value)
    {
        auto* const       renderer = static_cast<GraphicRenderer*>(context);
        const auto* const bake     = renderer ? renderer->m_sky_environment.GetActiveBake() : nullptr;
        if (bake)
            renderer->m_sky_environment.MarkGpuBakeStageSubmitted(bake->Revision, timeline_value);
    }

    void GraphicRenderer::ApplyGizmo(const GizmoFrameData& frame)
    {
        if (m_gizmo_pass)
            m_gizmo_pass->SetFrame(frame);
    }

    void GraphicRenderer::ApplyGridLabels(const GridLabelFrameData& frame)
    {
        if (m_grid_label_pass)
            m_grid_label_pass->SetFrame(frame);
    }

    void GraphicRenderer::ApplyGridConfig(const Scenes::SceneGridRenderSnapshot& cfg)
    {
        auto* pass = RenderGraph->GetPass("Grid Pass");
        if (!pass)
            return;
        static_cast<GridPass*>(pass->Callback)->SetConfig(cfg);
    }
} // namespace ZEngine::Rendering::Renderers
