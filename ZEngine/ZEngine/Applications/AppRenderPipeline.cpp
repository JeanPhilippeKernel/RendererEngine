#include <GLFW/glfw3.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/Windows/CoreWindow.h>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace
{
    // Gribb-Hartmann frustum extraction from a combined VP matrix (row-major, Vulkan NDC z∈[0,1]).
    // Each plane is stored as (nx, ny, nz, d) — normalized so distance = dot(n,p)+d.
    struct FrustumPlane
    {
        float x, y, z, w;
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
                return p;
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
        SceneRenderer           = ZPushStructCtor(Device->Arena, Rendering::Renderers::GraphicRenderer);
        ZUIRenderPass           = ZPushStructCtor(Device->Arena, Rendering::Renderers::ZUIPass);

        ZUIRenderPass->Initialize(Device);
        SceneRenderer->Initialize(Device);
        SceneRenderer->RenderGraph->AddCallbackPass("ZUI Draw Pass", ZUIRenderPass);

        // UIContext arena: created by Engine::Initialize via MemoryBudgetConfig::Editor().UIContext
        // (128 MB budgeted, ~60 MB committed: FrameArena 32 MB · PersistentArena 1 MB · ZUIPayloadArenas 9 MB × 3)
        auto* ui_arena = &Engine::GetContext()->UIContextArena;
        ZUICtx         = ZPushStructCtor(ui_arena, ZEngine::UI::ZUIContext);
        ZEngine::UI::ZUIContextInit(ZUICtx, ui_arena, ZMega(32), ZMega(1), 8192, 8192);
        for (int i = 0; i < 3; ++i)
        {
            ui_arena->CreateSubArena(ZMega(9), &ZUIPayloadArenas[i]);
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
        for (int i = 0; i < 3; ++i)
        {
            ZUIPayloadArenas[i].Shutdown();
        }
    }

    void AppRenderPipeline::ResizeRenderTarget(uint32_t w, uint32_t h)
    {
        if (w > 0 && h > 0 && SceneRenderer && SceneRenderer->RenderGraph)
            SceneRenderer->RenderGraph->Resize(w, h);
    }

    bool AppRenderPipeline::BeginFrame()
    {
        Device->FlushShaderReloadRequests();
        if (Device->PipelineStateCache)
            Device->PipelineStateCache->FlushAsyncPipelineJobs();

        auto swapchain = Device->SwapchainPtr;

        swapchain->AcquireNextImage(CurrentMailBoxBufferHead);

        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->BeginFrame(swapchain->CurrentFrame->Index);
        swapchain->FrameAsyncOperations.clear();
        swapchain->CollectAsyncGPUOperations();
        Managers::AssetManager::FlushTextureReleases();

        for (uint8_t thread_idx = 0; thread_idx < Device->CommandBufferMgr->TotalThreadCount; ++thread_idx)
        {
            Device->CommandBufferMgr->ResetPool(swapchain->CurrentFrame->Index, thread_idx);
            if (Device->RRM)
                static_cast<Rendering::RenderResourceManager*>(Device->RRM)->RetireTextureSlots(swapchain->CurrentFrame->Index, thread_idx);
        }

        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->CompleteDeferrals(swapchain->CurrentFrame->Index);

        CurrentCmdBuf = Device->CommandBufferMgr->GetCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, swapchain->CurrentFrame->Index, RenderMainThreadIndex, 0, false);
        vkResetCommandBuffer(CurrentCmdBuf->GetHandle(), 0);
        CurrentCmdBuf->ResetState();
        CurrentCmdBuf->Begin();

        return swapchain->IsFrameValid();
    }

    void AppRenderPipeline::EndFrame()
    {
        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->EndFrame();

        // A frame without UI geometry never enters dynamic rendering. The acquired
        // image must still be in PRESENT_SRC_KHR before the presentation bridge.
        CurrentCmdBuf->TransitionSwapchainImageToPresent();
        Device->CommandBufferMgr->EnqueueBuffer(CurrentCmdBuf);
        Device->CommandBufferMgr->EndEnqueuedBuffers();

        // Present before SubmitAsyncUploads: texture upload ops go into the deferred
        // queues and are waited on by the next frame's submit_1, not the current one.
        Device->SwapchainPtr->Present();
        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->SubmitAsyncUploads();
    }

    void AppRenderPipeline::RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene, const Rendering::Renderers::ZUIRenderPayload* overlay)
    {
        auto swpachain    = Device->SwapchainPtr;
        auto frame_index  = swpachain->CurrentFrame->Index;
        auto thread_index = RenderMainThreadIndex;

        if (scene->SkyDirty[frame_index].value.exchange(false, std::memory_order_acquire))
        {
            SceneRenderer->ApplySkyConfig(scene->Sky);
        }

        if (scene->GridDirty[frame_index].value.exchange(false, std::memory_order_acquire))
        {
            SceneRenderer->ApplyGridConfig(scene->Grid);
        }

        auto* gpu = SceneRenderer->RenderSceneData;

        // Clear the dirty flag — data is rebuilt every frame so culling tracks camera movement.
        scene->InstancesDirty[frame_index].value.exchange(false, std::memory_order_acquire);

        {
            auto*                                                    rrm     = Device->RRM ? reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM) : nullptr;
            auto*                                                    mgr     = Managers::AssetManager::Instance();
            auto                                                     scratch = ZGetScratch(&Engine::GetContext()->UIContextArena);

            Core::Containers::Array<Rendering::Scenes::MeshInstance> instances;
            scene->GetInstancesSnapshot(scratch.Arena, instances);

            // Extract camera frustum once for this frame.
            Mat4f        vp = camera->GetProjection() * camera->GetView();
            FrustumPlane planes[6];
            ExtractFrustumPlanes(vp, planes);

            Core::Containers::Array<Rendering::Meshes::SubMeshAllocation>   allocs;
            Core::Containers::Array<Rendering::Scenes::FrustumCullingInput> culling_inputs;
            Core::Containers::Array<Core::Maths::Mat4f>                     transforms;
            allocs.init(scratch.Arena, instances.size() * 4);
            culling_inputs.init(scratch.Arena, instances.size() * 4);
            transforms.init(scratch.Arena, instances.size());

            for (uint32_t inst_i = 0; inst_i < instances.size(); ++inst_i)
            {
                const auto& inst   = instances[inst_i];

                auto        handle = rrm ? rrm->FindMeshBuffer(inst.MeshUUID) : Rendering::BufferHandle{};
                if (!handle.IsValid())
                    continue;

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

                auto* mesh = mgr ? mgr->GetMeshAsset(inst.MeshUUID) : nullptr;
                if (!mesh)
                    continue;

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
                uint32_t transform_idx = static_cast<uint32_t>(transforms.size() - 1);

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
                    allocs.push(alloc);
                    VkDrawIndirectCommand draw = {.vertexCount = sub.IndexCount, .instanceCount = 1, .firstVertex = 0, .firstInstance = draw_idx};
                    culling_inputs.push({.WorldBounds = world_bounds, .Command = draw});
                }
            }

            gpu->IndirectCommandCount = static_cast<uint32_t>(culling_inputs.size());
            ZENGINE_VALIDATE_ASSERT(gpu->IndirectCommandCount <= Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS, "Too many draw commands — increase SceneData::MAX_DRAW_COMMANDS")

            if (rrm && gpu->TransformBuffers[frame_index].Handle && transforms.size() > 0)
                rrm->UpdateBuffer(gpu->TransformBuffers[frame_index], transforms.data(), transforms.size() * sizeof(Core::Maths::Mat4f));
            if (rrm && gpu->RenderDataBuffers[frame_index].Handle && allocs.size() > 0)
                rrm->UpdateBuffer(gpu->RenderDataBuffers[frame_index], allocs.data(), allocs.size() * sizeof(Rendering::Meshes::SubMeshAllocation));
            if (rrm && gpu->CullingInputBuffers[frame_index].Handle && culling_inputs.size() > 0)
                rrm->UpdateBuffer(gpu->CullingInputBuffers[frame_index], culling_inputs.data(), culling_inputs.size() * sizeof(Rendering::Scenes::FrustumCullingInput));

            for (uint32_t i = 0; i < 6; ++i)
                gpu->CullingPushConstants.FrustumPlanes[i] = Vec4f(planes[i].x, planes[i].y, planes[i].z, planes[i].w);
            gpu->CullingPushConstants.DrawCount = gpu->IndirectCommandCount;

            ZReleaseScratch(scratch);
        }

        if (Device->RRM)
        {
            auto* rrm      = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            auto* gpu_data = SceneRenderer->RenderSceneData;
            // Mark global buffers ready so draw guard allows rendering.
            if (!gpu_data->RMMVertexHandle.IsValid() && rrm->GlobalBuffersReady())
                gpu_data->RMMVertexHandle = {0, 1}; // sentinel — just needs IsValid() == true
        }

        if (Device->RRM)
        {
            auto* rrm     = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            auto* gpu_buf = SceneRenderer->RenderSceneData;
            if (gpu_buf->LightBuffers[frame_index].Handle)
                rrm->UpdateBuffer(gpu_buf->LightBuffers[frame_index], &scene->PendingLights, sizeof(Rendering::Scenes::LightArrayUBO));
        }

        ZUIRenderPass->SetPayload(overlay);
        CurrentCmdBuf = SceneRenderer->DrawScene(frame_index, thread_index, CurrentCmdBuf, camera);
        ZUIRenderPass->SetPayload(nullptr);
    }

    void AppRenderPipeline::BeginOverlayFrame(float dt)
    {
        if (ZUICtx)
        {
            // Use the logical window size (glfwGetWindowSize) not the physical swapchain
            // size. Mouse positions from GLFW cursor callbacks are also in logical pixels,
            // so panel positions and hit-testing must use the same coordinate space.
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
                        content_scale = 1.f;
                }

                // Exact ImGui approach (imgui_impl_glfw.cpp GetWindowSizeAndFramebufferScale):
                //   ScreenW/H = glfwGetWindowSize  → logical screen coords (1512 on Retina)
                //   UIScale   = glfwGetFramebufferSize / glfwGetWindowSize → physical/logical ratio (2.0 on Retina)
                //   Cursor from GLFW callback is already in logical space — no division needed.
                // This is identical on all platforms; macOS just happens to have UIScale=2 on Retina.
                if (native)
                {
                    int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
                    glfwGetWindowSize(native, &win_w, &win_h);
                    glfwGetFramebufferSize(native, &fb_w, &fb_h);
                    if (win_w > 0 && win_h > 0)
                    {
                        ZUICtx->ScreenW = (uint32_t) win_w;
                        ZUICtx->ScreenH = (uint32_t) win_h;
                        float s         = (fb_w > 0) ? (float) fb_w / (float) win_w : 1.f;
                        ZUICtx->UIScale = (s > 0.5f) ? s : 1.f;
                    }
                    else
                    {
                        ZUICtx->ScreenW = Device->CurrentWindow->GetWidth();
                        ZUICtx->ScreenH = Device->CurrentWindow->GetHeight();
                        ZUICtx->UIScale = content_scale;
                    }
                }
                else
                {
                    ZUICtx->ScreenW = Device->CurrentWindow->GetWidth();
                    ZUICtx->ScreenH = Device->CurrentWindow->GetHeight();
                    ZUICtx->UIScale = content_scale;
                }
                if (!ZUICtx->UIScaleLogged)
                {
                    ZENGINE_CORE_INFO("[ZUI] UIScale={:.2f} Screen={}x{} (logical) ContentScale={:.2f}", ZUICtx->UIScale, ZUICtx->ScreenW, ZUICtx->ScreenH, content_scale);
                    ZUICtx->UIScaleLogged = true;
                }
            }
            else
            {
                ZUICtx->ScreenW = Device->SwapchainPtr->SwapchainImageWidth;
                ZUICtx->ScreenH = Device->SwapchainPtr->SwapchainImageHeight;
            }
            ZEngine::UI::ZUIBeginFrame(ZUICtx, dt);
        }
    }

    void AppRenderPipeline::FillOverlayPayload(RenderPayload& payload)
    {
        if (ZUIRenderPass && ZUICtx && ZUICtx->Root)
        {
            uint32_t slot = MailBoxBufferHead.value.load(std::memory_order_relaxed);
            ZUIPayloadArenas[slot].Clear();
            ZUIRenderPass->PreparePayload(ZUICtx, &payload.ZUIOverlay, &ZUIPayloadArenas[slot]);
        }
    }

    void AppRenderPipeline::EndOverlayFrame()
    {
        if (ZUICtx)
        {
            ZEngine::UI::ZUIEndFrame(ZUICtx);
        }

        // Apply resize cursor from the ZUI divider hover state + flush clipboard writes
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
                int                req       = ZUICtx ? ZUICtx->ResizeCursor : 0;
                // Lazily create standard cursors (created once, never destroyed — app lifetime)
                static GLFWcursor* s_hresize = nullptr;
                static GLFWcursor* s_vresize = nullptr;
                if (!s_hresize)
                    s_hresize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
                if (!s_vresize)
                    s_vresize = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);

                if (req == 1 && s_hresize)
                    glfwSetCursor(gw, s_hresize);
                else if (req == 2 && s_vresize)
                    glfwSetCursor(gw, s_vresize);
                else
                    glfwSetCursor(gw, nullptr); // restore default
            }
        }
    }
} // namespace ZEngine::Applications
