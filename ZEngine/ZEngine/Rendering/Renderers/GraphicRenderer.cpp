#include <ZEngine/Engine.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Compute/FrustumCullingPass.h>
#include <ZEngine/Rendering/Renderers/Compute/SkyAtmosphereViewPass.h>
#include <ZEngine/Rendering/Renderers/Compute/SkyEnvironmentBakePass.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/DepthPrePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyCompositePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkySpherePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyboxPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/ToneMappingPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        uint32_t GetFullMipCount(uint32_t resolution)
        {
            uint32_t mip_count = 1;
            while (resolution > 1)
            {
                resolution >>= 1;
                ++mip_count;
            }
            return mip_count;
        }
    } // namespace

    GraphicRenderer::GraphicRenderer() {}
    GraphicRenderer::~GraphicRenderer() {}

    void GraphicRenderer::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device          = device;
        RenderGraph     = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);
        RenderSceneData = ZPushStructCtor(Device->Arena, Scenes::SceneData);
        ZENGINE_VALIDATE_ASSERT(Device->SwapchainPtr->BufferredFrameCount <= Scenes::SceneData::MAX_FRAMES_IN_FLIGHT, "SceneData buffers must cover every buffered frame")
        constexpr const char*  transform_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]       = {"TransformStorageBuffer[0]", "TransformStorageBuffer[1]", "TransformStorageBuffer[2]"};
        constexpr const char*  render_data_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]     = {"RenderDataStorageBuffer[0]", "RenderDataStorageBuffer[1]", "RenderDataStorageBuffer[2]"};
        constexpr const char*  material_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]        = {"MaterialStorageBuffer[0]", "MaterialStorageBuffer[1]", "MaterialStorageBuffer[2]"};
        constexpr const char*  light_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]           = {"LightStorageBuffer[0]", "LightStorageBuffer[1]", "LightStorageBuffer[2]"};
        constexpr const char*  culling_input_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]   = {"FrustumCullingInput[0]", "FrustumCullingInput[1]", "FrustumCullingInput[2]"};
        constexpr const char*  culled_indirect_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT] = {"FrustumCulledIndirect[0]", "FrustumCulledIndirect[1]", "FrustumCulledIndirect[2]"};
        constexpr VkDeviceSize culling_input_size                                             = Scenes::SceneData::MAX_DRAW_COMMANDS * sizeof(Scenes::FrustumCullingInput);
        constexpr VkDeviceSize culled_indirect_size                                           = Scenes::SceneData::MAX_DRAW_COMMANDS * sizeof(VkDrawIndirectCommand);
        for (uint32_t i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
        {
            RenderSceneData->TransformBuffers[i]      = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, transform_names[i]);
            RenderSceneData->RenderDataBuffers[i]     = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, render_data_names[i]);
            RenderSceneData->MaterialBuffers[i]       = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, material_names[i]);
            RenderSceneData->LightBuffers[i]          = Device->GpuMem.AllocateBuffer(sizeof(Scenes::LightArrayUBO), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, light_names[i]);
            RenderSceneData->CullingInputBuffers[i]   = Device->GpuMem.AllocateBuffer(culling_input_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, culling_input_names[i]);
            RenderSceneData->CulledIndirectBuffers[i] = Device->GpuMem.AllocateBuffer(culled_indirect_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, Core::Memory::GpuMemoryDomain::DeviceGeometry, culled_indirect_names[i]);
        }

        /*
         * Renderer Passes
         */
        auto scene_depth_prepass  = ZPushStructCtor(Device->Arena, DepthPrePass);
        auto frustum_culling_pass = ZPushStructCtor(Device->Arena, FrustumCullingPass);
        auto gbuffer_pass         = ZPushStructCtor(Device->Arena, GbufferPass);
        auto lighting_pass        = ZPushStructCtor(Device->Arena, LightingPass);
        auto skybox_pass          = ZPushStructCtor(Device->Arena, SkyboxPass);
        auto sky_sphere_pass      = ZPushStructCtor(Device->Arena, SkySpherePass);
        auto sky_view_lut_pass    = ZPushStructCtor(Device->Arena, SkyViewLutPass);
        auto aerial_pass          = ZPushStructCtor(Device->Arena, AerialPerspectivePass);
        auto sky_composite_pass   = ZPushStructCtor(Device->Arena, SkyCompositePass);
        auto grid_pass            = ZPushStructCtor(Device->Arena, GridPass);
        auto tone_mapping_pass    = ZPushStructCtor(Device->Arena, ToneMappingPass);

        RenderGraph->Initialize(Device, RenderSceneData);
        RenderGraph->ImportBuffer(RendererBufferName::Transform, &RenderSceneData->TransformBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::RenderData, &RenderSceneData->RenderDataBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::Material, &RenderSceneData->MaterialBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::Light, &RenderSceneData->LightBuffers[0]);
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
        m_lighting_pass                       = lighting_pass;
        m_grid_pass                           = grid_pass;
        m_skybox_pass                         = skybox_pass;
        m_sky_sphere_pass                     = sky_sphere_pass;
        m_sky_view_lut_pass                   = sky_view_lut_pass;
        m_aerial_perspective_pass             = aerial_pass;
        m_sky_composite_pass                  = sky_composite_pass;
        m_tone_mapping_pass                   = tone_mapping_pass;
        m_atmosphere_view_resources_supported = SupportsAtmosphereViewResources();
        m_sky_atmosphere_transmittance_pass   = ZPushStructCtorArgs(Device->Arena, SkyAtmosphereTransmittancePass, &m_sky_environment);
        m_sky_atmosphere_multiscattering_pass = ZPushStructCtorArgs(Device->Arena, SkyAtmosphereMultiscatteringPass, &m_sky_environment);
        m_sky_atmosphere_source_radiance_pass = ZPushStructCtorArgs(Device->Arena, SkyAtmosphereSourceRadiancePass, &m_sky_environment);
        m_sky_hdri_mip_generation_pass        = ZPushStructCtorArgs(Device->Arena, SkyEnvironmentMipGenerationPass, &m_sky_environment, "sky_environment_mip_generation", false);
        m_sky_atmosphere_mip_generation_pass  = ZPushStructCtorArgs(Device->Arena, SkyEnvironmentMipGenerationPass, &m_sky_environment, "sky_atmosphere_mip_generation", true);
        m_sky_diffuse_irradiance_pass         = ZPushStructCtorArgs(Device->Arena, SkyEnvironmentDiffuseIrradiancePass, &m_sky_environment);
        m_sky_specular_prefilter_pass         = ZPushStructCtorArgs(Device->Arena, SkyEnvironmentSpecularPrefilterPass, &m_sky_environment);
        m_lighting_pass->SetEnvironmentLighting(fallback_lighting, m_sky_environment.GetPresentationConfig());
        m_skybox_pass->SetEnvironment(fallback_environment, m_sky_environment.GetPresentationConfig());
        RenderGraph->ImportBuffer(RendererBufferName::GlobalVertex, rrm->GetGlobalVertexBuffer());
        RenderGraph->ImportBuffer(RendererBufferName::GlobalIndex, rrm->GetGlobalIndexBuffer());

        RenderGraph->AddCallbackPass("Frustum Culling Pass", frustum_culling_pass);
        RenderGraph->AddCallbackPass("Sky Atmosphere Transmittance", m_sky_atmosphere_transmittance_pass);
        RenderGraph->AddCallbackPass("Sky Atmosphere Multiscattering", m_sky_atmosphere_multiscattering_pass);
        RenderGraph->AddCallbackPass("Sky Atmosphere Source Radiance", m_sky_atmosphere_source_radiance_pass);
        RenderGraph->AddCallbackPass("Sky HDRI Source Mip Generation", m_sky_hdri_mip_generation_pass);
        RenderGraph->AddCallbackPass("Sky Atmosphere Source Mip Generation", m_sky_atmosphere_mip_generation_pass);
        RenderGraph->AddCallbackPass("Sky Diffuse Irradiance", m_sky_diffuse_irradiance_pass);
        RenderGraph->AddCallbackPass("Sky Specular Prefilter", m_sky_specular_prefilter_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);
        RenderGraph->AddCallbackPass("Sky Sphere Pass", sky_sphere_pass);
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass);
        RenderGraph->AddCallbackPass("Sky View LUT Pass", sky_view_lut_pass);
        RenderGraph->AddCallbackPass("Aerial Perspective Pass", aerial_pass);
        RenderGraph->AddCallbackPass("Sky Composite Pass", sky_composite_pass);
        RenderGraph->AddCallbackPass("Tone Mapping Pass", tone_mapping_pass);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);
        RenderGraph->Setup();
        RenderGraph->Compile();

        // No viewport texture is published here: before the ZUI pass is attached,
        // graph culling deliberately leaves FrameColor unallocated. DrawScene()
        // publishes the first real allocation after an acquired frame compiles it.
    }

    void GraphicRenderer::Deinitialize()
    {
        m_frame_output_sequence.value.fetch_add(1, std::memory_order_acq_rel);
        m_frame_output_index.value.store(UINT64_MAX, std::memory_order_relaxed);
        m_frame_output_generation.value.store(0, std::memory_order_relaxed);
        m_frame_output_sequence.value.fetch_add(1, std::memory_order_release);
        DiscardSkyResources(m_sky_environment.Shutdown());
        Scenes::SkyEnvironmentResources retired_sky_resources = {};
        while (m_sky_environment.TakeRetiredSnapshot(UINT64_MAX, retired_sky_resources))
            DiscardSkyResources(retired_sky_resources);
        m_lighting_pass                       = nullptr;
        m_grid_pass                           = nullptr;
        m_skybox_pass                         = nullptr;
        m_sky_sphere_pass                     = nullptr;
        m_sky_view_lut_pass                   = nullptr;
        m_aerial_perspective_pass             = nullptr;
        m_sky_composite_pass                  = nullptr;
        m_tone_mapping_pass                   = nullptr;
        m_sky_atmosphere_transmittance_pass   = nullptr;
        m_sky_atmosphere_multiscattering_pass = nullptr;
        m_sky_atmosphere_source_radiance_pass = nullptr;
        m_sky_hdri_mip_generation_pass        = nullptr;
        m_sky_atmosphere_mip_generation_pass  = nullptr;
        m_sky_diffuse_irradiance_pass         = nullptr;
        m_sky_specular_prefilter_pass         = nullptr;
        m_atmosphere_view_resources_supported = false;

        RenderGraph->Dispose();
        if (RenderSceneData)
        {
            for (uint32_t i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
            {
                Device->GpuMem.FreeBuffer(RenderSceneData->TransformBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->RenderDataBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->MaterialBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->LightBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->CullingInputBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->CulledIndirectBuffers[i]);
            }
        }
    }

    Hardwares::CommandBuffer* GraphicRenderer::DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, const Cameras::CameraFrameData& camera)
    {
        ZENGINE_VALIDATE_ASSERT(frame_index < Scenes::SceneData::MAX_FRAMES_IN_FLIGHT, "Invalid scene-buffer frame index")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Transform, &RenderSceneData->TransformBuffers[frame_index]), "Transform buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::RenderData, &RenderSceneData->RenderDataBuffers[frame_index]), "Render data buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Material, &RenderSceneData->MaterialBuffers[frame_index]), "Material buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Light, &RenderSceneData->LightBuffers[frame_index]), "Light buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::CullingInput, &RenderSceneData->CullingInputBuffers[frame_index]), "Culling input buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::CulledIndirect, &RenderSceneData->CulledIndirectBuffers[frame_index]), "Culled indirect buffer is not imported into the render graph")

        auto asset_manager   = Managers::AssetManager::Instance();
        auto view_proj       = camera.Projection * camera.View;
        auto ubo_camera_data = UBOCameraLayout{.View = camera.View, .Projection = camera.Projection, .Position = Vec4f(camera.Position, 1.0f), .InvViewProj = view_proj.Inverse()};

        if (Device->RRM && RenderSceneData->MaterialBuffers[frame_index].Handle)
        {
            auto* rrm = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            rrm->UpdateBuffer(RenderSceneData->MaterialBuffers[frame_index], asset_manager->GPUMeshMaterials.data(), asset_manager->GPUMeshMaterials.size() * sizeof(asset_manager->GPUMeshMaterials[0]));
        }

        // Light buffer is uploaded by AppRenderPipeline::RenderScene from scene->PendingLights.

        // Push camera data into the per-frame heap; store offset for dynamic descriptor binding
        auto& heap                             = Device->FrameHeaps[Device->SwapchainPtr->CurrentFrame->Index];
        auto  camera_alloc                     = heap.Push(&ubo_camera_data, sizeof(UBOCameraLayout), Device->MinUniformBufferOffsetAlignment());
        RenderSceneData->CameraHeapOffset      = camera_alloc.Offset;

        Hardwares::CommandBuffer* const output = RenderGraph->Execute(cb);
        PublishFrameOutput(RenderGraph->ResourceInspector->GetRenderTarget(RendererResourceName::FrameColorRenderTargetName));
        return output;
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

    void GraphicRenderer::ApplySkyConfig(const Scenes::SkyConfig& sky, const Scenes::SkyCelestialLight& celestial_light, uint64_t revision)
    {
        const EnvironmentLightingBakeSettings bake_settings       = Device ? Device->EnvironmentLightingBakeSettings : ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier::Standard);
        uint64_t                              hdri_source_hash    = 0;
        bool                                  hdri_artifact_ready = !sky.IsHDRI();
        if (sky.IsHDRI())
        {
            if (auto* const asset_manager = ZEngine::Managers::AssetManager::Instance(); asset_manager && asset_manager->Registry)
            {
                if (const auto* const environment = asset_manager->Registry->FindByUUID(sky.EnvironmentMap))
                {
                    hdri_source_hash    = environment->Meta.SourceHash;
                    hdri_artifact_ready = environment->State == Core::VFS::AssetState::Loaded && environment->Meta.ArtifactPath[0] != '\0';
                }
            }
        }

        if (m_sky_environment.SubmitConfig(sky, revision, bake_settings, celestial_light, hdri_source_hash, hdri_artifact_ready))
            StartPendingSkyBake();
        PollSkyBake();
    }

    void GraphicRenderer::BeginSkyFrame(const Cameras::CameraFrameData& camera)
    {
        PollSkyBake();
        StartPendingSkyBake();
        CollectRetiredSkySnapshots();

        if (!m_lighting_pass || !m_skybox_pass || !m_sky_sphere_pass || !m_grid_pass || !m_tone_mapping_pass || !m_sky_view_lut_pass || !m_aerial_perspective_pass || !m_sky_composite_pass)
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
        m_skybox_pass->SetUseSolidColorFallback(false);
        m_tone_mapping_pass->SetUseCompositedSceneColor(false);
        m_skybox_pass->SetEnabled(true);
        if (!snapshot)
            return;

        const Scenes::SkyConfig& presentation   = m_sky_environment.GetPresentationConfig();
        const bool               use_sky_sphere = presentation.IsSkySphere();
        m_lighting_pass->SetEnvironmentLighting(use_sky_sphere ? m_sky_environment.GetFallbackLighting() : snapshot->Lighting, presentation);
        m_skybox_pass->SetEnvironment(snapshot->SourceRadiance, presentation);
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
        m_skybox_pass->SetEnabled(!use_atmosphere_view && !use_sky_sphere);
        m_skybox_pass->SetUseSolidColorFallback(snapshot->Config.IsAtmosphere() && (view_class == Scenes::AtmosphereViewClass::BelowGround || view_class == Scenes::AtmosphereViewClass::Invalid));
        m_sky_view_lut_pass->SetEnvironment(use_atmosphere_view ? snapshot : nullptr, presentation);
        m_aerial_perspective_pass->SetEnvironment(use_atmosphere_view ? snapshot : nullptr, presentation);
        m_sky_composite_pass->SetEnvironment(use_atmosphere_view ? snapshot : nullptr, presentation);
        m_tone_mapping_pass->SetUseCompositedSceneColor(use_atmosphere_view);
        Device->SwapchainPtr->EnqueueRenderWorkSubmittedCallback(&GraphicRenderer::OnSkyFrameSubmitted, this, &GraphicRenderer::OnSkyFrameCancelled);
        if (m_sky_environment.CanRecordGpuBakeStage())
            Device->SwapchainPtr->EnqueueRenderWorkSubmittedCallback(&GraphicRenderer::OnSkyBakeStageSubmitted, this);
    }

    void GraphicRenderer::StartPendingSkyBake()
    {
        Scenes::SkyEnvironmentBakeRequest request = {};
        if (!m_sky_environment.TakeBakeRequest(request))
            return;

        if (request.Config.IsAtmosphere())
        {
            if (!request.BakeInputsValid || !SupportsAtmosphereBakeResources(request.BakeSettings))
            {
                ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: atmosphere requires valid resources and a selected directional light", request.Revision)
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

        auto* const asset_manager = ZEngine::Managers::AssetManager::Instance();
        auto* const rrm           = Device && Device->RRM ? static_cast<Rendering::RenderResourceManager*>(Device->RRM) : nullptr;
        if (!asset_manager || !asset_manager->Registry || !rrm)
        {
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: asset services are unavailable", request.Revision)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        const auto* const environment = asset_manager->Registry->FindByUUID(request.Config.EnvironmentMap);
        if (!environment)
        {
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: HDRI asset is not registered", request.Revision)
            m_sky_environment.CompleteBake(request.Revision, {}, false);
            return;
        }

        if (environment->Meta.ArtifactPath[0] == '\0')
        {
            ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: HDRI has no completed cooked artifact", request.Revision)
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

        char native_path[MAX_FILE_PATH_COUNT] = {};
        artifact_path.Value().ResolveNative(asset_manager->CurrentWorkingSpacePath, native_path, sizeof(native_path));
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
        if (!Device || !Device->SwapchainPtr || !Device->SwapchainPtr->RenderTimeline)
            return;

        uint64_t completed_timeline_value = 0;
        vkGetSemaphoreCounterValue(Device->LogicalDevice, Device->SwapchainPtr->RenderTimeline->GetHandle(), &completed_timeline_value);

        Scenes::SkyEnvironmentResources retired_resources = {};
        while (m_sky_environment.TakeRetiredSnapshot(completed_timeline_value, retired_resources))
            DiscardSkyResources(retired_resources);
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

    bool GraphicRenderer::SupportsAtmosphereBakeResources(const EnvironmentLightingBakeSettings& bake_settings) const
    {
        if (!Device || !bake_settings.IsValid())
            return false;

        constexpr VkFormat             kAtmosphereFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        constexpr VkFormatFeatureFlags kRequiredFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        VkFormatProperties             format_properties = {};
        vkGetPhysicalDeviceFormatProperties(Device->PhysicalDevice, kAtmosphereFormat, &format_properties);
        if ((format_properties.optimalTilingFeatures & kRequiredFeatures) != kRequiredFeatures)
            return false;

        VkImageFormatProperties cube_properties = {};
        const VkResult          cube_result     = vkGetPhysicalDeviceImageFormatProperties(Device->PhysicalDevice, kAtmosphereFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, &cube_properties);
        if (cube_result != VK_SUCCESS || cube_properties.maxExtent.width < bake_settings.SourceRadianceResolution || cube_properties.maxExtent.height < bake_settings.SourceRadianceResolution || cube_properties.maxArrayLayers < 6)
            return false;

        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(Device->PhysicalDevice, &properties);
        return properties.limits.maxImageDimension2D >= 256 && properties.limits.maxImageDimensionCube >= bake_settings.SourceRadianceResolution;
    }

    bool GraphicRenderer::SupportsAtmosphereViewResources() const
    {
        if (!Device)
            return false;

        constexpr VkFormat             kAtmosphereFormat   = VK_FORMAT_R16G16B16A16_SFLOAT;
        constexpr VkFormatFeatureFlags kRequiredFeatures   = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        constexpr VkImageUsageFlags    kRequiredImageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        VkFormatProperties             format_properties   = {};
        vkGetPhysicalDeviceFormatProperties(Device->PhysicalDevice, kAtmosphereFormat, &format_properties);
        if ((format_properties.optimalTilingFeatures & kRequiredFeatures) != kRequiredFeatures)
            return false;

        VkImageFormatProperties volume_properties = {};
        const VkResult          volume_result     = vkGetPhysicalDeviceImageFormatProperties(Device->PhysicalDevice, kAtmosphereFormat, VK_IMAGE_TYPE_3D, VK_IMAGE_TILING_OPTIMAL, kRequiredImageUsage, 0, &volume_properties);
        return volume_result == VK_SUCCESS && volume_properties.maxExtent.width >= 32 && volume_properties.maxExtent.height >= 32 && volume_properties.maxExtent.depth >= 32;
    }

    Scenes::AtmosphereStaticResources GraphicRenderer::CreateAtmosphereStaticResources()
    {
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
        resources.Transmittance                     = Device->CreateTexture(transmittance_spec, "SkyAtmosphereTransmittance");
        resources.Multiscattering                   = Device->CreateTexture(multiscattering_spec, "SkyAtmosphereMultiscattering");
        if (!resources.Valid())
        {
            DiscardSkyResources({.Atmosphere = resources});
            return {};
        }
        return resources;
    }

    Textures::TextureHandle GraphicRenderer::CreateAtmosphereSourceRadiance(const EnvironmentLightingBakeSettings& bake_settings)
    {
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
        return Device->CreateTexture(source_spec, "SkyAtmosphereSourceRadiance");
    }

    EnvironmentLightingResources GraphicRenderer::CreateSkyLightingResources(const EnvironmentLightingBakeSettings& bake_settings)
    {
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
        resources.DiffuseIrradiance            = Device->CreateTexture(diffuse_spec, "SkyDiffuseIrradiance");
        resources.SpecularEnvironment          = Device->CreateTexture(specular_spec, "SkySpecularEnvironment");
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

    void GraphicRenderer::ApplyGridConfig(const Scenes::GridConfig& cfg)
    {
        auto* pass = RenderGraph->GetPass("Grid Pass");
        if (!pass)
            return;
        auto* grid_pass    = static_cast<GridPass*>(pass->Callback);
        grid_pass->Enabled = cfg.Enabled;
        if (!cfg.Enabled)
            return;

        auto& p        = grid_pass->PushData;
        p.CellSize     = cfg.CellSize;
        p.FadeRadius   = cfg.FadeRadius;
        p.FadeStrength = cfg.FadeStrength;
        p.LineWidth    = cfg.LineWidth;
        p.MaxLOD       = cfg.MaxLOD;
        p.GroundY      = cfg.GroundY;
        secure_memcpy(p.ColorThin, sizeof(p.ColorThin), cfg.ColorThin, sizeof(cfg.ColorThin));
        secure_memcpy(p.ColorThick, sizeof(p.ColorThick), cfg.ColorThick, sizeof(cfg.ColorThick));
        secure_memcpy(p.ColorXAxis, sizeof(p.ColorXAxis), cfg.ColorXAxis, sizeof(cfg.ColorXAxis));
        secure_memcpy(p.ColorZAxis, sizeof(p.ColorZAxis), cfg.ColorZAxis, sizeof(cfg.ColorZAxis));
    }
} // namespace ZEngine::Rendering::Renderers
