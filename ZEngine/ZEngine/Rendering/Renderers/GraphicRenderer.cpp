#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Compute/FrustumCullingPass.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/DepthPrePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyboxPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering::Renderers
{
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
        auto grid_pass            = ZPushStructCtor(Device->Arena, GridPass);

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
        m_sky_environment.Initialize(fallback_environment, fallback_lighting);
        m_lighting_pass = lighting_pass;
        m_skybox_pass   = skybox_pass;
        m_lighting_pass->SetEnvironmentLighting(fallback_lighting, m_sky_environment.GetPresentationConfig());
        m_skybox_pass->SetEnvironment(fallback_environment, m_sky_environment.GetPresentationConfig());
        RenderGraph->ImportBuffer(RendererBufferName::GlobalVertex, rrm->GetGlobalVertexBuffer());
        RenderGraph->ImportBuffer(RendererBufferName::GlobalIndex, rrm->GetGlobalIndexBuffer());

        RenderGraph->AddCallbackPass("Frustum Culling Pass", frustum_culling_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass);
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
        const Textures::TextureHandle active_bake_texture = m_sky_environment.Shutdown();
        if (active_bake_texture.Valid())
            Device->DestroyTexture(active_bake_texture);
        Textures::TextureHandle retired_sky_texture = {};
        while (m_sky_environment.TakeRetiredSnapshot(UINT64_MAX, retired_sky_texture))
            Device->DestroyTexture(retired_sky_texture);
        m_lighting_pass = nullptr;
        m_skybox_pass   = nullptr;

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

    void GraphicRenderer::ApplySkyConfig(const Scenes::SkyConfig& sky, uint64_t revision)
    {
        if (m_sky_environment.SubmitConfig(sky, revision))
            StartPendingSkyBake();
        PollSkyBake();
    }

    void GraphicRenderer::BeginSkyFrame()
    {
        PollSkyBake();
        StartPendingSkyBake();
        CollectRetiredSkySnapshots();

        const Scenes::SkyEnvironmentSnapshot* snapshot = m_sky_environment.AcquireForFrame();
        if (!snapshot || !m_lighting_pass || !m_skybox_pass)
            return;

        const Scenes::SkyConfig& presentation = m_sky_environment.GetPresentationConfig();
        m_lighting_pass->SetEnvironmentLighting(snapshot->Lighting, presentation);
        m_skybox_pass->SetEnvironment(snapshot->SourceRadiance, presentation);
        Device->SwapchainPtr->EnqueueRenderWorkSubmittedCallback(&GraphicRenderer::OnSkyFrameSubmitted, this, &GraphicRenderer::OnSkyFrameCancelled);
    }

    void GraphicRenderer::StartPendingSkyBake()
    {
        Scenes::SkyEnvironmentBakeRequest request = {};
        if (!m_sky_environment.TakeBakeRequest(request))
            return;

        // HDRI source preparation is asynchronous. Per-scene diffuse and
        // specular convolution remain on the engine-global fallback until #805.
        if (!request.Config.IsHDRI() || request.Config.EnvironmentMap.is_nil())
        {
            ZENGINE_CORE_WARN("[SkyEnvironment] Revision {} is using the fallback: {} source baking is not implemented yet", request.Revision, request.Config.IsHDRI() ? "an HDRI without an asset" : "analytic")
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

        char native_path[MAX_FILE_PATH_COUNT] = {};
        environment->Path.ResolveNative(asset_manager->CurrentWorkingSpacePath, native_path, sizeof(native_path));
        if (native_path[0] == '\0')
        {
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} is using the fallback: HDRI path cannot be resolved", request.Revision)
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

        auto* const                                                rrm          = static_cast<Rendering::RenderResourceManager*>(Device->RRM);
        const Rendering::RenderResourceManager::TextureDecodeState decode_state = rrm->GetTextureDecodeState(source_radiance);
        if (decode_state == Rendering::RenderResourceManager::TextureDecodeState::Pending)
            return;

        const uint64_t revision = bake->Revision;
        if (decode_state != Rendering::RenderResourceManager::TextureDecodeState::Succeeded)
        {
            rrm->ForgetTextureDecode(source_radiance);
            m_sky_environment.CompleteBake(revision, source_radiance, false);
            Device->DestroyTexture(source_radiance);
            ZENGINE_CORE_ERROR("[SkyEnvironment] Revision {} failed to decode; retaining the previous ready environment or fallback", revision)
            StartPendingSkyBake();
            return;
        }

        const Hardwares::StreamingUploadTicket* const ticket = rrm->FindStreamingUploadTicket(source_radiance);
        if (!ticket || !ticket->CompletionTimeline)
            return;

        uint64_t completed_value = 0;
        vkGetSemaphoreCounterValue(Device->LogicalDevice, ticket->CompletionTimeline->GetHandle(), &completed_value);
        if (completed_value < ticket->CompletionValue)
            return;

        const Hardwares::StreamingUploadTicket completed_ticket = *ticket;
        rrm->ForgetTextureDecode(source_radiance);
        const Scenes::SkyEnvironmentBakeResult result = m_sky_environment.CompleteBake(revision, source_radiance, true);
        if (result == Scenes::SkyEnvironmentBakeResult::Published)
        {
            ZENGINE_CORE_INFO("[SkyEnvironment] Published HDRI revision {}", revision)
        }
        else
        {
            // This source was never imported into a graph pass. Its upload is
            // complete, so it can be discarded without invalidating a frame.
            rrm->AcknowledgeStreamingUploadTicket(completed_ticket);
            Device->DestroyTexture(source_radiance);
            if (result == Scenes::SkyEnvironmentBakeResult::Discarded)
                ZENGINE_CORE_INFO("[SkyEnvironment] Discarded stale HDRI revision {}", revision)
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

        Textures::TextureHandle retired_source = {};
        while (m_sky_environment.TakeRetiredSnapshot(completed_timeline_value, retired_source))
            DiscardSkyTexture(retired_source);
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
