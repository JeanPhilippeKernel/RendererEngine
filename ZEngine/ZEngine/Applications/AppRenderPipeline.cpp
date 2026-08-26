#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <GLFW/glfw3.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>

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

    bool SphereInFrustum(const FrustumPlane planes[6], Vec3f center, float radius)
    {
        for (int i = 0; i < 6; ++i)
        {
            float d = planes[i].x * center.x + planes[i].y * center.y + planes[i].z * center.z + planes[i].w;
            if (d < -radius)
                return false;
        }
        return true;
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
        Device->Arena->CreateSubArena(ZMega(30), &LocalArena);

        SceneRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::GraphicRenderer);
        ZUIRenderer   = ZPushStructCtor(Device->Arena, Rendering::Renderers::ZUIRenderer);

        SceneRenderer->Initialize(Device);
        ZUIRenderer->Initialize(Device);

        // ZPushStructCtor calls placement-new → default member initializers apply
        // (ZPushStruct only zero-fills, so ZUITheme colours would all be 0).
        ZUICtx = ZPushStructCtor(&LocalArena, ZEngine::UI::ZUIContext);
        ZEngine::UI::ZUIContextInit(ZUICtx, &LocalArena, ZMega(4), ZMega(1), 4096, 4096);
        for (int i = 0; i < 3; ++i)
        {
            LocalArena.CreateSubArena(ZMega(4), &ZUIPayloadArenas[i]);
        }

        Device->SwapchainPtr->OnSwapchainResized    = [](uint32_t w, uint32_t h, void* ctx) { static_cast<AppRenderPipeline*>(ctx)->ResizeRenderTarget(w, h); };
        Device->SwapchainPtr->OnSwapchainResizedCtx = this;

    }

    void AppRenderPipeline::Shutdown()
    {
        SceneRenderer->Deinitialize();
        if (ZUIRenderer) { ZUIRenderer->Deinitialize(); }
        if (ZUICtx)      { ZEngine::UI::ZUIContextDestroy(ZUICtx); }
        for (int i = 0; i < 3; ++i) { ZUIPayloadArenas[i].Shutdown(); }
    }

    void AppRenderPipeline::ResizeRenderTarget(uint32_t w, uint32_t h)
    {
        if (SceneRenderer && SceneRenderer->RenderGraph)
            SceneRenderer->RenderGraph->Resize(w, h);
    }

    bool AppRenderPipeline::BeginFrame()
    {
        auto swapchain = Device->SwapchainPtr;

        swapchain->AcquireNextImage(CurrentMailBoxBufferHead);

        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->BeginFrame(swapchain->CurrentFrame->Index);

        for (uint8_t thread_idx = 0; thread_idx < Device->CommandBufferMgr->TotalThreadCount; ++thread_idx)
        {
            Device->CommandBufferMgr->ResetPool(swapchain->CurrentFrame->Index, thread_idx);
            if (Device->RRM)
                static_cast<Rendering::RenderResourceManager*>(Device->RRM)->RetireTextureSlots(swapchain->CurrentFrame->Index, thread_idx);
        }

        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->CompleteDeferrals();

        CurrentCmdBuf = Device->CommandBufferMgr->GetCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, swapchain->CurrentFrame->Index, RenderMainThreadIndex, 0, false);
        vkResetCommandBuffer(CurrentCmdBuf->GetHandle(), 0);
        CurrentCmdBuf->ResetState();
        CurrentCmdBuf->Begin();

        return swapchain->IsFrameValid();
    }

    void AppRenderPipeline::EndFrame()
    {
        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->SubmitTextureJobs();
        Device->CommandBufferMgr->EnqueueBuffer(CurrentCmdBuf);
        Device->CommandBufferMgr->EndEnqueuedBuffers();

        Device->SwapchainPtr->Present();

        // RRM::EndFrame AFTER Present so swap entries drain with the correct frame counter.
        if (Device->RRM)
            static_cast<Rendering::RenderResourceManager*>(Device->RRM)->EndFrame(Device->SwapchainPtr->CurrentFrame->Index);
    }

    void AppRenderPipeline::RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene)
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
            auto                                                     scratch = ZGetScratch(&LocalArena);

            Core::Containers::Array<Rendering::Scenes::MeshInstance> instances;
            scene->GetInstancesSnapshot(scratch.Arena, instances);

            // Extract camera frustum once for this frame.
            Mat4f        vp = camera->GetProjection() * camera->GetView();
            FrustumPlane planes[6];
            ExtractFrustumPlanes(vp, planes);

            Core::Containers::Array<Rendering::Meshes::SubMeshAllocation> allocs;
            Core::Containers::Array<VkDrawIndirectCommand>                draws;
            Core::Containers::Array<Core::Maths::Mat4f>                   transforms;
            allocs.init(scratch.Arena, instances.size() * 4);
            draws.init(scratch.Arena, instances.size() * 4);
            transforms.init(scratch.Arena, instances.size());

            for (uint32_t inst_i = 0; inst_i < instances.size(); ++inst_i)
            {
                const auto& inst   = instances[inst_i];

                auto        handle = rrm ? rrm->FindMeshBuffer(inst.MeshUUID) : Rendering::BufferHandle{};
                if (!handle.IsValid())
                    continue;

                uint32_t vtx_base = 0, idx_base = 0;
                rrm->GetMeshOffsets(handle, vtx_base, idx_base);

                auto* mesh = mgr ? mgr->GetMeshAsset(inst.MeshUUID) : nullptr;
                if (!mesh)
                    continue;

                // Frustum cull — sphere test in world space.
                if (mesh->BoundsRadius > 0.f)
                {
                    const Vec3f& c = mesh->BoundsCenter;
                    Vec3f        worldCenter(inst.Transform(0, 0) * c.x + inst.Transform(0, 1) * c.y + inst.Transform(0, 2) * c.z + inst.Transform(0, 3), inst.Transform(1, 0) * c.x + inst.Transform(1, 1) * c.y + inst.Transform(1, 2) * c.z + inst.Transform(1, 3), inst.Transform(2, 0) * c.x + inst.Transform(2, 1) * c.y + inst.Transform(2, 2) * c.z + inst.Transform(2, 3));
                    float        worldRadius = mesh->BoundsRadius * MaxColumnScale(inst.Transform);
                    if (!SphereInFrustum(planes, worldCenter, worldRadius))
                        continue;
                }

                transforms.push(inst.Transform);
                uint32_t transform_idx = static_cast<uint32_t>(transforms.size() - 1);

                for (uint32_t sub_i = 0; sub_i < static_cast<uint32_t>(mesh->SubMeshes.size()); ++sub_i)
                {
                    const auto&                          sub      = mesh->SubMeshes[sub_i];
                    auto*                                mat      = Managers::AssetManager::GetAsset<Importers::AssetMaterial>(sub.MaterialUUID);
                    uint32_t                             mat_idx  = mat ? static_cast<uint32_t>(mat - mgr->Materials.data()) : 0;
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
                    draws.push({.vertexCount = sub.IndexCount, .instanceCount = 1, .firstVertex = 0, .firstInstance = draw_idx});
                }
            }

            if (rrm && gpu->TransformBuffer.Handle && transforms.size() > 0)
                rrm->UpdateBuffer(gpu->TransformBuffer, transforms.data(), transforms.size() * sizeof(Core::Maths::Mat4f));
            if (rrm && gpu->RenderDataBuffer.Handle && allocs.size() > 0)
                rrm->UpdateBuffer(gpu->RenderDataBuffer, allocs.data(), allocs.size() * sizeof(Rendering::Meshes::SubMeshAllocation));

            gpu->IndirectCommandCount = static_cast<uint32_t>(draws.size());
            ZENGINE_VALIDATE_ASSERT(gpu->IndirectCommandCount <= Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS, "Too many draw commands — increase SceneData::MAX_DRAW_COMMANDS")
            for (uint32_t dc = 0; dc < gpu->IndirectCommandCount; ++dc)
                gpu->CachedDrawCmds[dc] = draws[dc];

            ZReleaseScratch(scratch);
        }

        // Always push draw commands to the heap this frame (heap resets every frame).
        if (gpu->IndirectCommandCount > 0)
        {
            auto& heap              = Device->FrameHeaps[frame_index];
            auto  indirect_alloc    = heap.Push(gpu->CachedDrawCmds, gpu->IndirectCommandCount * sizeof(VkDrawIndirectCommand), sizeof(VkDrawIndirectCommand));
            gpu->IndirectHeapOffset = indirect_alloc.Offset;
        }

        if (Device->RRM)
        {
            auto* rrm      = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            auto* gpu_data = SceneRenderer->RenderSceneData;
            // Mark global buffers ready so draw guard allows rendering.
            if (!gpu_data->RMMVertexHandle.IsValid() && rrm->GlobalBuffersReady())
                gpu_data->RMMVertexHandle = {0, 1}; // sentinel — just needs IsValid() == true
            SceneRenderer->UpdateRMMBindings(gpu_data);
        }

        if (Device->RRM)
        {
            auto* rrm     = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            auto* gpu_buf = SceneRenderer->RenderSceneData;
            if (gpu_buf->LightBuffer.Handle)
                rrm->UpdateBuffer(gpu_buf->LightBuffer, &scene->PendingLights, sizeof(Rendering::Scenes::LightArrayUBO));
        }

        SceneRenderer->DrawScene(frame_index, thread_index, CurrentCmdBuf, camera);
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
                auto* native = static_cast<GLFWwindow*>(Device->CurrentWindow->GetNativeWindow());
                float content_scale = 1.f;
                if (native)
                {
                    float xs = 1.f, ys = 1.f;
                    glfwGetWindowContentScale(native, &xs, &ys);
                    content_scale = (xs > ys ? xs : ys);
                    if (content_scale < 0.5f) content_scale = 1.f;
                }

#if defined(__APPLE__)
                // macOS (Gemini-verified): glfwGetWindowSize returns PHYSICAL pixels.
                // UI logical size = physical / ContentScale.  Cursor is also physical →
                // ZUILayer::OnMouseButtonMoved divides by UIScale before storing MousePos.
                ZUICtx->UIScale = content_scale;
                ZUICtx->ScreenW = (uint32_t)((float)Device->CurrentWindow->GetWidth()  / content_scale);
                ZUICtx->ScreenH = (uint32_t)((float)Device->CurrentWindow->GetHeight() / content_scale);
#else
                // Windows/Linux: glfwGetWindowSize returns logical pixels already.
                // UIScale = Fb/Win ratio for actual HiDPI framebuffers.
                ZUICtx->ScreenW = Device->CurrentWindow->GetWidth();
                ZUICtx->ScreenH = Device->CurrentWindow->GetHeight();
                if (native)
                {
                    int fb_w = 0, fb_h = 0;
                    glfwGetFramebufferSize(native, &fb_w, &fb_h);
                    float s = (ZUICtx->ScreenW > 0 && fb_w > 0)
                             ? (float)fb_w / (float)ZUICtx->ScreenW : 1.f;
                    ZUICtx->UIScale = s > 0.5f ? s : 1.f;
                }
                else { ZUICtx->UIScale = content_scale; }
#endif
                if (!ZUICtx->UIScaleLogged && native)
                {
                    int fb_w = 0, fb_h = 0;
                    glfwGetFramebufferSize(native, &fb_w, &fb_h);
                    ZENGINE_CORE_INFO(
                        "[ZUI] ContentScale={:.2f} UIScale={:.2f} Screen={}x{} Win={}x{} Fb={}x{}",
                        content_scale, ZUICtx->UIScale,
                        ZUICtx->ScreenW, ZUICtx->ScreenH,
                        Device->CurrentWindow->GetWidth(), Device->CurrentWindow->GetHeight(),
                        fb_w, fb_h);
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
        if (ZUIRenderer && ZUICtx && ZUICtx->Root)
        {
            uint32_t slot = MailBoxBufferHead.value.load(std::memory_order_relaxed);
            ZUIPayloadArenas[slot].Clear();
            ZUIRenderer->PreparePayload(ZUICtx, &payload.ZUIOverlay, &ZUIPayloadArenas[slot]);
        }
    }

    void AppRenderPipeline::RenderOverlay(const RenderPayload& payload)
    {
        if (ZUIRenderer) { ZUIRenderer->Submit(CurrentCmdBuf, payload.ZUIOverlay); }
    }

    void AppRenderPipeline::EndOverlayFrame()
    {
        if (ZUICtx) { ZEngine::UI::ZUIEndFrame(ZUICtx); }
    }
} // namespace ZEngine::Applications
