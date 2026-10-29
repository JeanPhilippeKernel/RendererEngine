#include <GLFW/glfw3.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <cmath>
#include <limits>
#include <memory>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace
{
    // Gribb-Hartmann frustum extraction from a combined VP matrix (row-major, Vulkan NDC z∈[0,1]).
    // Each plane is stored as (nx, ny, nz, d) — normalized so distance = dot(n,p)+d.
    struct FrustumPlane
    {
        float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
    };

    void ExtractFrustumPlanes(const Mat4f& vp, FrustumPlane out[6])
    {
        // Row vectors
        auto row       = [&](int r) -> FrustumPlane { return {vp(r, 0), vp(r, 1), vp(r, 2), vp(r, 3)}; };
        auto add       = [](FrustumPlane a, FrustumPlane b) -> FrustumPlane { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; };
        auto sub       = [](FrustumPlane a, FrustumPlane b) -> FrustumPlane { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; };
        auto normalize = [](FrustumPlane p) -> FrustumPlane {
            float len = Vec3f(p.x, p.y, p.z).magnitude();
            if (len < 1e-6f)
            {
                return p;
            }
            return {p.x / len, p.y / len, p.z / len, p.w / len};
        };

        out[0] = normalize(add(row(3), row(0))); // left
        out[1] = normalize(sub(row(3), row(0))); // right
        out[2] = normalize(add(row(3), row(1))); // bottom
        out[3] = normalize(sub(row(3), row(1))); // top
        out[4] = normalize(row(2));              // near  (Vulkan z≥0)
        out[5] = normalize(sub(row(3), row(2))); // far
    }

    float MaxColumnScale(const Mat4f& m)
    {
        float s0 = Vec3f(m(0, 0), m(1, 0), m(2, 0)).magnitude();
        float s1 = Vec3f(m(0, 1), m(1, 1), m(2, 1)).magnitude();
        float s2 = Vec3f(m(0, 2), m(1, 2), m(2, 2)).magnitude();
        return s0 > s1 ? (s0 > s2 ? s0 : s2) : (s1 > s2 ? s1 : s2);
    }
} // anonymous namespace

namespace ZEngine::Applications
{
    void AppRenderPipeline::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device                  = device;
        RenderWorkerThreadCount = Device->CommandBufferMgr->TotalThreadCount > 0u ? Device->CommandBufferMgr->TotalThreadCount - 1u : 0u;
        SceneRenderer           = ZPushStructCtor<Rendering::Renderers::GraphicRenderer>(Device->Arena);
        ZUIRenderPass           = ZPushStructCtor<Rendering::Renderers::ZUIPass>(Device->Arena);

        ZUIRenderPass->Initialize(Device);
        SceneRenderer->Initialize(Device);
        SceneRenderer->RenderGraph->AddCallbackPass("ZUI Draw Pass", ZUIRenderPass);

        m_next_frame_state_sequence = 1;
        m_next_overlay_sequence     = 1;
        OverlayBuildAvailable.value.store(true, std::memory_order_relaxed);
        for (uint32_t slot = 0; slot < MaxFrameStateBufferCount; ++slot)
        {
            FrameStates[slot] = {};
            FrameStateSlots[slot].value.store(static_cast<uint32_t>(PayloadSlotState::Free), std::memory_order_relaxed);
            FrameStateSequences[slot].value.store(0, std::memory_order_relaxed);
        }
        for (uint32_t slot = 0; slot < MaxOverlayBufferCount; ++slot)
        {
            OverlayPayloads[slot] = {};
            OverlayPayloadStates[slot].value.store(static_cast<uint32_t>(PayloadSlotState::Free), std::memory_order_relaxed);
        }

        // UIContext arena: created by Engine::Initialize via MemoryBudgetConfig::Editor().UIContext
        // (128 MB budgeted: UI arenas ~60 MB, immutable scene-instance slots ~15 MB).
        auto* ui_arena = &Engine::GetContext()->UIContextArena;
        InitializeFrameStorage(ui_arena);
        ZUICtx = ZPushStructCtor<ZEngine::UI::ZUIContext>(ui_arena);
        ZEngine::UI::ZUIContextInit(ZUICtx, ui_arena, ZMega(32), ZMega(1), 8192, 8192);
        for (uint32_t i = 0; i < MaxOverlayBufferCount; ++i)
        {
            ui_arena->CreateSubArena(ZMega(9), &ZUIPayloadArenas[i], "UIContext/OverlayPayload");
        }

        // The scene graph owns editor-viewport-sized images, while the UI pass
        // resolves the current swapchain image at record time.  A window/swapchain
        // recreation must therefore not resize the viewport targets: doing so
        // replaces their image views with the window extent and races panel-driven
        // resize requests.  Legacy swapchain framebuffers are recreated by
        // DeviceSwapchain itself.
        Device->SwapchainPtr->OnSwapchainResized    = nullptr;
        Device->SwapchainPtr->OnSwapchainResizedCtx = nullptr;
    }

    void AppRenderPipeline::Shutdown()
    {
        SceneRenderer->Deinitialize();
        if (ZUICtx)
        {
            ZEngine::UI::ZUIContextDestroy(ZUICtx);
        }
        for (uint32_t i = 0; i < MaxOverlayBufferCount; ++i)
        {
            ZUIPayloadArenas[i].Shutdown();
        }
    }

    void AppRenderPipeline::ResizeRenderTarget(uint32_t w, uint32_t h)
    {
        if (w > 0 && h > 0 && SceneRenderer && SceneRenderer->RenderGraph)
        {
            SceneRenderer->RenderGraph->Resize(w, h);
        }
    }

    bool AppRenderPipeline::BeginFrame()
    {
        Device->FlushShaderReloadRequests();
        if (Device->PipelineStateCache)
        {
            Device->PipelineStateCache->FlushAsyncPipelineJobs();
        }

        auto swapchain = Device->SwapchainPtr;

        swapchain->AcquireNextImage(CurrentFrameContextIndex);
        if (!swapchain->IsFrameValid())
        {
            return false;
        }

        if (Device->RRM)
        {
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->BeginFrame(swapchain->CurrentFrame->Index);
        }
        swapchain->FrameAsyncOperations.clear();
        swapchain->TraceSubmission = false;
        swapchain->CollectAsyncGPUOperations();
        Managers::AssetManager::FlushTextureReleases();

        for (uint8_t thread_idx = 0; thread_idx < Device->CommandBufferMgr->TotalThreadCount; ++thread_idx)
        {
            Device->CommandBufferMgr->ResetPool(swapchain->CurrentFrame->Index, thread_idx);
            if (Device->RRM)
            {
                static_cast<Rendering::RenderResourceManager*>(Device->RRM)->RetireTextureSlots(swapchain->CurrentFrame->Index, thread_idx);
            }
        }

        if (Device->RRM)
        {
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->CompleteDeferrals(swapchain->CurrentFrame->Index);
        }

        CurrentCmdBuf = Device->CommandBufferMgr->GetCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, swapchain->CurrentFrame->Index, RenderMainThreadIndex, 0, false);
        vkResetCommandBuffer(CurrentCmdBuf->GetHandle(), 0);
        CurrentCmdBuf->ResetState();
        CurrentCmdBuf->Begin();

        return swapchain->IsFrameValid();
    }

    void AppRenderPipeline::EndFrame()
    {
        if (!Device->SwapchainPtr->IsFrameValid())
        {
            Device->SwapchainPtr->Present();
            return;
        }
        if (Device->RRM)
        {
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->EndFrame();
        }

        // A frame without UI geometry never enters dynamic rendering. The acquired
        // image must still be in PRESENT_SRC_KHR before the presentation bridge.
        CurrentCmdBuf->TransitionSwapchainImageToPresent();
        Device->CommandBufferMgr->EnqueueBuffer(CurrentCmdBuf);
        Device->CommandBufferMgr->EndEnqueuedBuffers();

        // Present before SubmitAsyncUploads: texture upload ops go into the deferred
        // queues and are waited on by the next frame's submit_1, not the current one.
        Device->SwapchainPtr->Present();
        if (Device->RRM)
        {
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->SubmitAsyncUploads();
        }
    }

    void AppRenderPipeline::RenderScene(const RenderFrameState& state, const Rendering::Renderers::ZUIRenderPayload* overlay)
    {
        const auto& camera       = state.Camera;
        const auto& scene        = state.Scene;
        auto        swpachain    = Device->SwapchainPtr;
        auto        frame_index  = swpachain->CurrentFrame->Index;
        auto        thread_index = RenderMainThreadIndex;

        SceneRenderer->ApplySkyConfig(state.Sky, state.CelestialLight, state.SkyRevision);
        SceneRenderer->ApplyGridConfig(scene.Grid);
        Rendering::Renderers::GridLabelFrameData grid_labels = {};
        (void) Rendering::Renderers::BuildGridLabelFrame(scene.Grid, camera, state.GridLabelFont, state.RenderTargetW, state.RenderTargetH, grid_labels);
        SceneRenderer->ApplyGridLabels(grid_labels);
        SceneRenderer->ApplyGizmo(state.Gizmo);
        SceneRenderer->ApplySelectionOutline(state.SelectionOutline.Enabled());

        auto*      gpu                = SceneRenderer->RenderSceneData;
        // Begin exactly once. A mailbox may retain the same request for several
        // render opportunities while it waits for a free consumer slot.
        const auto object_pick        = state.ObjectPick;
        const auto hover_pick         = state.HoverPick;
        const bool record_object_pick = object_pick.Valid() && object_pick.Begin(object_pick.Context);
        const bool record_hover_pick  = hover_pick.Valid() && hover_pick.Begin(hover_pick.Context);

        {
            auto*        rrm       = Device->RRM ? reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM) : nullptr;
            auto*        mgr       = Managers::AssetManager::Instance();
            auto         scratch   = ZGetScratch(&Engine::GetContext()->UIContextArena);

            // Extract camera frustum once for this frame.
            Mat4f        vp        = camera.Projection * camera.View;
            FrustumPlane planes[6] = {};
            ExtractFrustumPlanes(vp, planes);

            Core::Containers::Array<Rendering::Meshes::SubMeshAllocation>   allocs;
            Core::Containers::Array<Rendering::Scenes::FrustumCullingInput> culling_inputs;
            Core::Containers::Array<Core::Maths::Mat4f>                     transforms;
            Core::Containers::Array<uint32_t>                               selection_token_states;
            allocs.init(scratch.Arena, scene.InstanceCount * 4);
            culling_inputs.init(scratch.Arena, scene.InstanceCount * 4);
            transforms.init(scratch.Arena, scene.InstanceCount);
            if (state.SelectionOutline.Enabled())
            {
                selection_token_states.init(scratch.Arena, Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS + 1, Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS + 1);
                ZEngine::Helpers::secure_memset(selection_token_states.data(), 0, selection_token_states.size() * sizeof(uint32_t), selection_token_states.size() * sizeof(uint32_t));
            }

            for (uint32_t inst_i = 0; inst_i < scene.InstanceCount; ++inst_i)
            {
                const auto& inst   = scene.Instances[inst_i];

                auto        handle = rrm ? rrm->FindMeshBuffer(inst.MeshUUID) : Rendering::BufferHandle{};
                if (!handle.IsValid())
                {
                    continue;
                }

                // Skip non-resident meshes — either upload still in flight (Pending) or the
                // slot was evicted (Unloaded). For evicted slots, enqueue a reload so the
                // streaming manager re-uploads the data at the start of the next frame.
                if (!rrm->IsMeshResident(handle))
                {
                    rrm->RequestMeshLoad(handle, inst.MeshUUID);
                    continue;
                }

                rrm->MarkMeshReferenced(handle);

                uint32_t vtx_base = 0, idx_base = 0;
                rrm->GetMeshOffsets(handle, vtx_base, idx_base);

                if (!mgr)
                {
                    continue;
                }
                // CPU descriptors/material slots can grow while an uncached scene
                // asset is published. Keep borrowed data inside the ingest lease;
                // GPU lookups above and buffer submissions below hold no lease.
                std::lock_guard asset_lock(mgr->IngestMutex);
                auto*           mesh = mgr->GetMeshAsset(inst.MeshUUID);
                if (!mesh)
                {
                    continue;
                }

                // Build a world-space sphere for the GPU culling pass. A mesh with
                // no authored bound is deliberately retained as always visible.
                Vec4f world_bounds(0.f, 0.f, 0.f, -1.f);
                if (mesh->BoundsRadius > 0.f)
                {
                    const Vec3f& c = mesh->BoundsCenter;
                    Vec3f        worldCenter(inst.Transform(0, 0) * c.x + inst.Transform(0, 1) * c.y + inst.Transform(0, 2) * c.z + inst.Transform(0, 3), inst.Transform(1, 0) * c.x + inst.Transform(1, 1) * c.y + inst.Transform(1, 2) * c.z + inst.Transform(1, 3), inst.Transform(2, 0) * c.x + inst.Transform(2, 1) * c.y + inst.Transform(2, 2) * c.z + inst.Transform(2, 3));
                    float        worldRadius = mesh->BoundsRadius * MaxColumnScale(inst.Transform);
                    world_bounds             = Vec4f(worldCenter, worldRadius);
                }

                transforms.push(inst.Transform);
                uint32_t       transform_idx  = static_cast<uint32_t>(transforms.size() - 1);
                // This state is shared by every submesh of the instance.
                // Compute it once before assigning one pick token per draw.
                const bool     selected       = state.SelectionOutline.Contains(inst.Entity);
                const bool     hovered        = !selected && inst.Entity == state.SelectionOutline.Hovered;
                const uint32_t feedback_state = selected ? 2u : hovered ? 1u : 0u;
                const bool     needs_token    = inst.Entity.IsValid() && (record_object_pick || record_hover_pick || feedback_state != 0);

                for (uint32_t sub_i = 0; sub_i < static_cast<uint32_t>(mesh->SubMeshes.size()); ++sub_i)
                {
                    const auto&                          sub      = mesh->SubMeshes[sub_i];
                    uint32_t*                            mat_slot = mgr ? mgr->UUIDToMaterialSlot.find(sub.MaterialUUID) : nullptr;
                    uint32_t                             mat_idx  = mat_slot ? *mat_slot : 0;
                    uint32_t                             draw_idx = static_cast<uint32_t>(allocs.size());

                    Rendering::Meshes::SubMeshAllocation alloc    = {};
                    alloc.VertexOffset                            = vtx_base + sub.VertexOffset;
                    alloc.VertexCount                             = sub.VertexCount;
                    alloc.IndexOffset                             = idx_base + sub.IndexOffset;
                    alloc.IndexCount                              = sub.IndexCount;
                    alloc.InstanceCount                           = 1;
                    alloc.TransformId                             = transform_idx;
                    alloc.MaterialId                              = mat_idx;
                    // The same frame-local token image supports independent click
                    // and hover readbacks plus selected-state outline feedback.
                    alloc.PickToken                               = needs_token && draw_idx < Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS ? draw_idx + 1 : 0;
                    if (record_object_pick && alloc.PickToken != 0)
                    {
                        object_pick.RecordToken(object_pick.Context, alloc.PickToken, inst.Entity);
                    }
                    if (record_hover_pick && alloc.PickToken != 0)
                    {
                        hover_pick.RecordToken(hover_pick.Context, alloc.PickToken, inst.Entity);
                    }
                    if (feedback_state != 0 && alloc.PickToken != 0)
                    {
                        selection_token_states[alloc.PickToken] = feedback_state;
                    }
                    allocs.push(alloc);
                    VkDrawIndirectCommand draw = {.vertexCount = sub.IndexCount, .instanceCount = 1, .firstVertex = 0, .firstInstance = draw_idx};
                    culling_inputs.push({.WorldBounds = world_bounds, .Command = draw});
                }
            }

            gpu->IndirectCommandCount = static_cast<uint32_t>(culling_inputs.size());
            ZENGINE_VALIDATE_ASSERT(gpu->IndirectCommandCount <= Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS, "Too many draw commands — increase SceneData::MAX_DRAW_COMMANDS")

            if (rrm && gpu->TransformBuffers[frame_index].Handle && transforms.size() > 0)
            {
                rrm->UpdateBuffer(gpu->TransformBuffers[frame_index], transforms.data(), transforms.size() * sizeof(Core::Maths::Mat4f));
            }
            if (rrm && gpu->RenderDataBuffers[frame_index].Handle && allocs.size() > 0)
            {
                rrm->UpdateBuffer(gpu->RenderDataBuffers[frame_index], allocs.data(), allocs.size() * sizeof(Rendering::Meshes::SubMeshAllocation));
            }
            if (rrm && gpu->CullingInputBuffers[frame_index].Handle && culling_inputs.size() > 0)
            {
                rrm->UpdateBuffer(gpu->CullingInputBuffers[frame_index], culling_inputs.data(), culling_inputs.size() * sizeof(Rendering::Scenes::FrustumCullingInput));
            }
            if (rrm && state.SelectionOutline.Enabled() && gpu->SelectionTokenStateBuffers[frame_index].Handle)
            {
                rrm->UpdateBuffer(gpu->SelectionTokenStateBuffers[frame_index], selection_token_states.data(), selection_token_states.size() * sizeof(uint32_t));
            }

            for (uint32_t i = 0; i < 6; ++i)
            {
                gpu->CullingPushConstants.FrustumPlanes[i] = Vec4f(planes[i].x, planes[i].y, planes[i].z, planes[i].w);
            }
            gpu->CullingPushConstants.DrawCount = gpu->IndirectCommandCount;

            ZReleaseScratch(scratch);
        }

        if (Device->RRM)
        {
            auto* rrm      = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            auto* gpu_data = SceneRenderer->RenderSceneData;
            // Mark global buffers ready so draw guard allows rendering.
            if (!gpu_data->RMMVertexHandle.IsValid() && rrm->GlobalBuffersReady())
            {
                gpu_data->RMMVertexHandle = {0, 1}; // sentinel — just needs IsValid() == true
            }
        }

        if (Device->RRM)
        {
            auto* rrm     = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            auto* gpu_buf = SceneRenderer->RenderSceneData;
            if (gpu_buf->LightBuffers[frame_index].Handle)
            {
                rrm->UpdateBuffer(gpu_buf->LightBuffers[frame_index], &scene.Lights, sizeof(Rendering::Scenes::LightArrayUBO));
            }
        }

        ZUIRenderPass->SetPayload(overlay);
        if (record_object_pick)
        {
            SceneRenderer->RequestObjectIdPick({.X = object_pick.X, .Y = object_pick.Y, .Callback = object_pick.Complete, .Cancelled = object_pick.Cancelled, .Context = object_pick.Context});
            // The request has been declared before DrawScene rebuilds the graph.
            // The completion callback owns the final Submitted -> Completed edge.
            object_pick.Submitted(object_pick.Context);
        }
        if (record_hover_pick)
        {
            SceneRenderer->RequestObjectIdPick({.X = hover_pick.X, .Y = hover_pick.Y, .Callback = hover_pick.Complete, .Cancelled = hover_pick.Cancelled, .Context = hover_pick.Context});
            hover_pick.Submitted(hover_pick.Context);
        }
        SceneRenderer->BeginSkyFrame(camera);
        CurrentCmdBuf = SceneRenderer->DrawScene(frame_index, thread_index, CurrentCmdBuf, camera);
        ZUIRenderPass->SetPayload(nullptr);
    }

    void AppRenderPipeline::BeginOverlayFrame(float dt)
    {
        if (ZUICtx)
        {
            const uint32_t previous_screen_w = ZUICtx->ScreenW;
            const uint32_t previous_screen_h = ZUICtx->ScreenH;
            const float    previous_scale    = ZUICtx->UIScale;
            // ZUI uses DPI-independent coordinates.  The render pass projects them into
            // the physical swapchain, while input is converted to the same UI space.
            if (Device->CurrentWindow)
            {
                auto* native        = static_cast<GLFWwindow*>(Device->CurrentWindow->GetNativeWindow());
                float content_scale = 1.f;
                if (native)
                {
                    float xs = 1.f, ys = 1.f;
                    glfwGetWindowContentScale(native, &xs, &ys);
                    content_scale = (xs > ys ? xs : ys);
                    if (content_scale < 0.5f)
                    {
                        content_scale = 1.f;
                    }
                }

                // GLFW's content scale is the UI-metric scale.  On Wayland and macOS it
                // normally matches framebuffer/window, while X11 can report a scaled
                // desktop with a 1:1 framebuffer.  Build one DPI-independent ZUI space
                // from the larger factor so text and controls remain readable on all
                // three platforms.
                int win_w = (int) Device->CurrentWindow->GetWidth();
                int win_h = (int) Device->CurrentWindow->GetHeight();
                int fb_w  = win_w;
                int fb_h  = win_h;
                if (native)
                {
                    glfwGetWindowSize(native, &win_w, &win_h);
                    glfwGetFramebufferSize(native, &fb_w, &fb_h);
                }

                win_w                           = win_w > 0 ? win_w : 1;
                win_h                           = win_h > 0 ? win_h : 1;
                fb_w                            = fb_w > 0 ? fb_w : win_w;
                fb_h                            = fb_h > 0 ? fb_h : win_h;

                const float framebuffer_scale_x = (float) fb_w / (float) win_w;
                const float framebuffer_scale_y = (float) fb_h / (float) win_h;
                const float ui_scale            = fmaxf(0.5f, fmaxf(content_scale, fmaxf(framebuffer_scale_x, framebuffer_scale_y)));

                ZUICtx->ScreenW                 = (uint32_t) fmaxf(1.f, roundf((float) fb_w / ui_scale));
                ZUICtx->ScreenH                 = (uint32_t) fmaxf(1.f, roundf((float) fb_h / ui_scale));
                ZUICtx->UIScale                 = (float) fb_w / (float) ZUICtx->ScreenW;
                ZUICtx->InputScale[0]           = (float) ZUICtx->ScreenW / (float) win_w;
                ZUICtx->InputScale[1]           = (float) ZUICtx->ScreenH / (float) win_h;
                if (!ZUICtx->UIScaleLogged || previous_screen_w != ZUICtx->ScreenW || previous_screen_h != ZUICtx->ScreenH || previous_scale != ZUICtx->UIScale)
                {
                    ZENGINE_CORE_INFO("[ZUI] UIScale={:.2f} Screen={}x{} Window={}x{} Framebuffer={}x{} ContentScale={:.2f} InputScale={:.2f}x{:.2f}", ZUICtx->UIScale, ZUICtx->ScreenW, ZUICtx->ScreenH, win_w, win_h, fb_w, fb_h, content_scale, ZUICtx->InputScale[0], ZUICtx->InputScale[1]);
                    ZUICtx->UIScaleLogged = true;
                }
            }
            else
            {
                ZUICtx->ScreenW       = Device->SwapchainPtr->SwapchainImageWidth;
                ZUICtx->ScreenH       = Device->SwapchainPtr->SwapchainImageHeight;
                ZUICtx->UIScale       = 1.f;
                ZUICtx->InputScale[0] = 1.f;
                ZUICtx->InputScale[1] = 1.f;
            }
            ZEngine::UI::ZUIBeginFrame(ZUICtx, dt);
        }
    }

    void AppRenderPipeline::FillOverlayPayload(OverlayPayload& payload, uint32_t payload_slot)
    {
        ZENGINE_VALIDATE_ASSERT(payload_slot < MaxOverlayBufferCount, "AppRenderPipeline::FillOverlayPayload: invalid payload slot")

        payload.ZUIOverlay = {};
        if (ZUIRenderPass && ZUICtx && ZUICtx->Root)
        {
            ZUIPayloadArenas[payload_slot].Clear();
            ZUIRenderPass->PreparePayload(ZUICtx, &payload.ZUIOverlay, &ZUIPayloadArenas[payload_slot]);
        }
    }

    void AppRenderPipeline::InitializeFrameStorage(Core::Memory::ArenaAllocator* arena, uint32_t max_instances)
    {
        ZENGINE_VALIDATE_ASSERT(arena && m_frame_instance_capacity == 0 && max_instances > 0 && max_instances <= Rendering::Scenes::SceneFrameData::MaxInstances, "Invalid frame snapshot storage initialization")
        for (auto& instances : m_frame_instances)
        {
            instances = static_cast<Rendering::Scenes::SceneDrawInstance*>(arena->AllocateNoZero(sizeof(Rendering::Scenes::SceneDrawInstance) * max_instances, alignof(Rendering::Scenes::SceneDrawInstance)));
            ZENGINE_VALIDATE_ASSERT(instances, "Unable to allocate bounded frame snapshot storage")
        }
        m_frame_instance_capacity = max_instances;
    }

    bool AppRenderPipeline::PublishFrameState(const RenderFrameState& state, const Rendering::Scenes::RenderScene* source_scene)
    {
        constexpr uint32_t free_state    = static_cast<uint32_t>(PayloadSlotState::Free);
        constexpr uint32_t ready_state   = static_cast<uint32_t>(PayloadSlotState::Ready);
        constexpr uint32_t writing_state = static_cast<uint32_t>(PayloadSlotState::Writing);

        // Reject before claiming a slot: an invalid frame must not evict valid unread data.
        if (!state.Gizmo.ValidCounts() || !state.Gizmo.ValidMeshLineGroups())
        {
            return false;
        }
        const auto instance_count = source_scene ? source_scene->Instances.size() : 0;
        if (source_scene && (!m_frame_instance_capacity || instance_count > m_frame_instance_capacity))
        {
            if (!m_snapshot_capacity_reported)
                ZENGINE_CORE_WARN("Render snapshot requires {} instances; capacity is {}. Scene publication rejected without truncation.", instance_count, m_frame_instance_capacity)
            m_snapshot_capacity_reported = true;
            return false;
        }
        m_snapshot_capacity_reported = false;

        // Prefer a free slot, then replace the oldest unread state.
        uint32_t selected_slot       = MaxFrameStateBufferCount;
        uint64_t oldest_sequence     = std::numeric_limits<uint64_t>::max();
        bool     claimed             = false;
        for (uint32_t slot = 0; slot < MaxFrameStateBufferCount; ++slot)
        {
            uint32_t expected = free_state;
            if (FrameStateSlots[slot].value.compare_exchange_strong(expected, writing_state, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                selected_slot = slot;
                claimed       = true;
                break;
            }

            const uint64_t sequence = FrameStateSequences[slot].value.load(std::memory_order_acquire);
            if (expected == ready_state && sequence < oldest_sequence)
            {
                oldest_sequence = sequence;
                selected_slot   = slot;
            }
        }

        if (selected_slot == MaxFrameStateBufferCount)
        {
            return false;
        }

        uint32_t expected = ready_state;
        if (!claimed && !FrameStateSlots[selected_slot].value.compare_exchange_strong(expected, writing_state, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return false;
        }

        auto& destination = FrameStates[selected_slot];
        destination       = state;
        // Source access is exclusively main-thread, after hierarchy/light synchronization.
        // The render consumer sees no borrowed scene, even when that source is later destroyed.
        destination.Scene = {};
        if (source_scene)
        {
            auto*          instances = m_frame_instances[selected_slot];
            const uint32_t count     = static_cast<uint32_t>(instance_count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& source = source_scene->Instances[i];
                std::construct_at(instances + i, Rendering::Scenes::SceneDrawInstance{source.Transform, source.MeshUUID, source.Entity});
            }
            destination.Scene.Instances     = instances;
            destination.Scene.InstanceCount = count;
            destination.Scene.Lights        = source_scene->PendingLights;
            destination.Scene.Grid          = source_scene->Grid;
            destination.Scene.Grid.Enabled  = source_scene->Grid.Enabled && state.ShowGrid;
            destination.Scene.HasScene      = true;
            destination.Sky                 = source_scene->Sky;
            destination.CelestialLight      = source_scene->CelestialLight;
            destination.SkyRevision         = source_scene->SkyRevision.value.load(std::memory_order_acquire);
        }
        FrameStateSequences[selected_slot].value.store(m_next_frame_state_sequence++, std::memory_order_relaxed);
        FrameStateSlots[selected_slot].value.store(ready_state, std::memory_order_release);
        return true;
    }

    bool AppRenderPipeline::TryReadFrameState(RenderFrameState& state)
    {
        constexpr uint32_t free_state    = static_cast<uint32_t>(PayloadSlotState::Free);
        constexpr uint32_t ready_state   = static_cast<uint32_t>(PayloadSlotState::Ready);
        constexpr uint32_t reading_state = static_cast<uint32_t>(PayloadSlotState::Reading);

        for (;;)
        {
            uint32_t newest_slot     = MaxFrameStateBufferCount;
            uint64_t newest_sequence = 0;
            for (uint32_t slot = 0; slot < MaxFrameStateBufferCount; ++slot)
            {
                if (FrameStateSlots[slot].value.load(std::memory_order_acquire) == ready_state)
                {
                    const uint64_t sequence = FrameStateSequences[slot].value.load(std::memory_order_relaxed);
                    if (sequence > newest_sequence)
                    {
                        newest_slot     = slot;
                        newest_sequence = sequence;
                    }
                }
            }

            if (newest_slot == MaxFrameStateBufferCount)
            {
                return false;
            }

            uint32_t expected = ready_state;
            if (!FrameStateSlots[newest_slot].value.compare_exchange_strong(expected, reading_state, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                continue;
            }

            // A producer can replace Ready while we scan. Inspect the actual sequence only
            // after acquiring ownership, and never return a state older than the last read.
            newest_sequence = FrameStateSequences[newest_slot].value.load(std::memory_order_relaxed);
            if (newest_sequence <= m_last_read_frame_sequence)
            {
                FrameStateSlots[newest_slot].value.store(free_state, std::memory_order_release);
                continue;
            }
            state = FrameStates[newest_slot];

            // Claim before checking age to avoid an ABA race discarding a newly published
            // state. A newer racing publication must remain available for the next read.
            for (uint32_t slot = 0; slot < MaxFrameStateBufferCount; ++slot)
            {
                if (slot == newest_slot)
                {
                    continue;
                }
                expected = ready_state;
                if (FrameStateSlots[slot].value.compare_exchange_strong(expected, reading_state, std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    const auto sequence = FrameStateSequences[slot].value.load(std::memory_order_relaxed);
                    FrameStateSlots[slot].value.store(sequence <= newest_sequence ? free_state : ready_state, std::memory_order_release);
                }
            }

            ReleaseFrameState();
            m_retained_frame_slot      = newest_slot;
            m_last_read_frame_sequence = newest_sequence;
            return true;
        }
    }

    void AppRenderPipeline::ReleaseFrameState()
    {
        if (m_retained_frame_slot < MaxFrameStateBufferCount)
        {
            FrameStateSlots[m_retained_frame_slot].value.store(static_cast<uint32_t>(PayloadSlotState::Free), std::memory_order_release);
            m_retained_frame_slot = MaxFrameStateBufferCount;
        }
    }

    bool AppRenderPipeline::BeginOverlayWrite(OverlayPayload*& payload, uint32_t& payload_slot)
    {
        constexpr uint32_t free_state     = static_cast<uint32_t>(PayloadSlotState::Free);
        constexpr uint32_t write_state    = static_cast<uint32_t>(PayloadSlotState::Writing);

        // The render thread releases one token after each completed frame. This
        // keeps costly UI generation presentation-paced while retaining a
        // dedicated latest-state mailbox for input and camera updates.
        bool               expected_token = true;
        if (!OverlayBuildAvailable.value.compare_exchange_strong(expected_token, false, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            payload      = nullptr;
            payload_slot = 0;
            return false;
        }

        for (uint32_t slot = 0; slot < MaxOverlayBufferCount; ++slot)
        {
            uint32_t expected = free_state;
            if (OverlayPayloadStates[slot].value.compare_exchange_strong(expected, write_state, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                payload      = &OverlayPayloads[slot];
                payload_slot = slot;
                return true;
            }
        }

        // A token with no free slot is transient (for example while a frame is
        // handing an overlay over). Return it rather than suppressing the next
        // eligible UI build.
        OverlayBuildAvailable.value.store(true, std::memory_order_release);
        payload      = nullptr;
        payload_slot = 0;
        return false;
    }

    void AppRenderPipeline::PublishOverlay(uint32_t payload_slot)
    {
        ZENGINE_VALIDATE_ASSERT(payload_slot < MaxOverlayBufferCount, "AppRenderPipeline::PublishOverlay: invalid payload slot")
        ZENGINE_VALIDATE_ASSERT(OverlayPayloadStates[payload_slot].value.load(std::memory_order_relaxed) == static_cast<uint32_t>(PayloadSlotState::Writing), "AppRenderPipeline::PublishOverlay: slot is not being written")

        OverlayPayloads[payload_slot].Sequence = m_next_overlay_sequence++;
        OverlayPayloadStates[payload_slot].value.store(static_cast<uint32_t>(PayloadSlotState::Ready), std::memory_order_release);
    }

    bool AppRenderPipeline::BeginOverlayRead(OverlayPayload*& payload, uint32_t& payload_slot)
    {
        constexpr uint32_t ready_state = static_cast<uint32_t>(PayloadSlotState::Ready);
        constexpr uint32_t read_state  = static_cast<uint32_t>(PayloadSlotState::Reading);
        constexpr uint32_t free_state  = static_cast<uint32_t>(PayloadSlotState::Free);

        // Select the latest payload and recycle older unread payloads.
        for (;;)
        {
            uint32_t newest_slot     = MaxOverlayBufferCount;
            uint64_t newest_sequence = 0;
            for (uint32_t slot = 0; slot < MaxOverlayBufferCount; ++slot)
            {
                if (OverlayPayloadStates[slot].value.load(std::memory_order_acquire) == ready_state && OverlayPayloads[slot].Sequence > newest_sequence)
                {
                    newest_slot     = slot;
                    newest_sequence = OverlayPayloads[slot].Sequence;
                }
            }

            if (newest_slot == MaxOverlayBufferCount)
            {
                payload      = nullptr;
                payload_slot = 0;
                return false;
            }

            uint32_t expected = ready_state;
            if (OverlayPayloadStates[newest_slot].value.compare_exchange_strong(expected, read_state, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                // The selected slot is now immutable. Any older ready overlay
                // can be safely recycled; its arena is never cleared until a
                // later writer claims that slot.
                for (uint32_t slot = 0; slot < MaxOverlayBufferCount; ++slot)
                {
                    if (slot == newest_slot)
                    {
                        continue;
                    }
                    expected = ready_state;
                    OverlayPayloadStates[slot].value.compare_exchange_strong(expected, free_state, std::memory_order_acq_rel, std::memory_order_acquire);
                }

                payload      = &OverlayPayloads[newest_slot];
                payload_slot = newest_slot;
                return true;
            }
        }
    }

    void AppRenderPipeline::EndOverlayRead(uint32_t payload_slot)
    {
        ZENGINE_VALIDATE_ASSERT(payload_slot < MaxOverlayBufferCount, "AppRenderPipeline::EndOverlayRead: invalid payload slot")
        ZENGINE_VALIDATE_ASSERT(OverlayPayloadStates[payload_slot].value.load(std::memory_order_relaxed) == static_cast<uint32_t>(PayloadSlotState::Reading), "AppRenderPipeline::EndOverlayRead: slot is not being read")
        OverlayPayloadStates[payload_slot].value.store(static_cast<uint32_t>(PayloadSlotState::Free), std::memory_order_release);
    }

    void AppRenderPipeline::NotifyOverlayFrameComplete()
    {
        OverlayBuildAvailable.value.store(true, std::memory_order_release);
    }

    void AppRenderPipeline::EndOverlayFrame()
    {
        if (ZUICtx)
        {
            ZEngine::UI::ZUIEndFrame(ZUICtx);
        }

        // Resolve the semantic cursor selected by this UI frame and flush
        // clipboard writes. Cursor handles remain process-lifetime GLFW objects.
        if (Device && Device->CurrentWindow)
        {
            auto* gw = static_cast<GLFWwindow*>(Device->CurrentWindow->GetNativeWindow());
            if (gw)
            {
                // Flush Ctrl+C clipboard write (set by ZUITextField when focused)
                if (ZUICtx && ZUICtx->ClipboardWrite[0] != '\0')
                {
                    glfwSetClipboardString(gw, ZUICtx->ClipboardWrite);
                    ZUICtx->ClipboardWrite[0] = '\0';
                }
                const auto         request     = ZUICtx ? ZUICtx->Cursor : ZEngine::UI::ZUICursor::Default;
                static GLFWcursor* s_crosshair = nullptr;
                static GLFWcursor* s_hresize   = nullptr;
                static GLFWcursor* s_vresize   = nullptr;
                if (!s_crosshair)
                {
                    s_crosshair = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
                }
                if (!s_hresize)
                {
                    s_hresize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
                }
                if (!s_vresize)
                {
                    s_vresize = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
                }
                if (request == ZEngine::UI::ZUICursor::Crosshair && s_crosshair)
                {
                    glfwSetCursor(gw, s_crosshair);
                }
                else if (request == ZEngine::UI::ZUICursor::HorizontalResize && s_hresize)
                {
                    glfwSetCursor(gw, s_hresize);
                }
                else if (request == ZEngine::UI::ZUICursor::VerticalResize && s_vresize)
                {
                    glfwSetCursor(gw, s_vresize);
                }
                else
                {
                    glfwSetCursor(gw, nullptr); // restore default
                }
            }
        }
    }
} // namespace ZEngine::Applications
