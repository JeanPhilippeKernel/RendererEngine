#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>

using namespace ZEngine::Core::Containers;

namespace ZEngine::Applications
{
    void AppRenderPipeline::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device                  = device;
        RenderWorkerThreadCount = Device->CommandBufferMgr->TotalThreadCount > 0u ? Device->CommandBufferMgr->TotalThreadCount - 1u : 0u;
        UICommandBufferIndex    = RenderMainThreadIndex + 1u;
        Device->Arena->CreateSubArena(ZMega(30), &LocalArena);

        SceneRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::GraphicRenderer);
        ImguiRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::ImGUIRenderer);

        SceneRenderer->Initialize(Device);
        ImguiRenderer->Initialize(Device);

        Device->SwapchainPtr->OnSwapchainResized    = [](uint32_t w, uint32_t h, void* ctx) { static_cast<AppRenderPipeline*>(ctx)->ResizeRenderTarget(w, h); };
        Device->SwapchainPtr->OnSwapchainResizedCtx = this;

        for (size_t i = 0; i < MaxMailBoxBufferCount; ++i)
        {
            RenderPayloads[i].UIOverlay.IndexedCmds.resize(100);
            RenderPayloads[i].UIOverlay.ScissorCmds.resize(100);
            RenderPayloads[i].UIOverlay.TextureIds.resize(100);
        }
    }

    void AppRenderPipeline::Shutdown()
    {
        SceneRenderer->Deinitialize();
        ImguiRenderer->Deinitialize();
    }

    void AppRenderPipeline::ResizeRenderTarget(uint32_t w, uint32_t h)
    {
        if (SceneRenderer && SceneRenderer->RenderGraph)
        {
            auto rendergraph = SceneRenderer->RenderGraph;
            rendergraph->Resize(w, h);
        }
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

        if (scene->InstancesDirty[frame_index].value.exchange(false, std::memory_order_acquire))
        {
            auto*                                                    rrm     = Device->RRM ? reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM) : nullptr;
            auto*                                                    mgr     = Managers::AssetManager::Instance();

            auto                                                     scratch = ZGetScratch(&LocalArena);

            // Seqlock snapshot — consistent copy of the instance list.
            Core::Containers::Array<Rendering::Scenes::MeshInstance> instances;
            scene->GetInstancesSnapshot(scratch.Arena, instances);

            // Pre-size scratch arrays (worst-case: each mesh has multiple submeshes).
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

            // Cache draw commands — the heap resets each frame so we must re-push
            // every frame. The cache holds the latest snapshot.
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

        SceneRenderer->DrawScene(frame_index, thread_index, CurrentCmdBuf, camera);
    }

    void AppRenderPipeline::BeginOverlayFrame()
    {
        ImguiRenderer->NewFrame();
    }

    void AppRenderPipeline::FillOverlayPayload(Rendering::Renderers::RenderOverlayPayload& payload)
    {
        ImguiRenderer->PreparePayload(payload);
    }

    void AppRenderPipeline::RenderOverlay(const Rendering::Renderers::RenderOverlayPayload& payload)
    {
        if (payload.VertexCount == 0 && payload.IndexCount == 0)
        {
            return;
        }

        auto     swpachain           = Device->SwapchainPtr;
        auto     frame_index         = swpachain->CurrentFrame->Index;
        auto     thread_index        = RenderMainThreadIndex;

        auto     current_framebuffer = Device->SwapchainPtr->SwapchainFramebuffers[Device->SwapchainPtr->CurrentFrame->ImageIndex];

        // Resolve per-frame ImGui buffers now that BeginFrame has set CurrentFrame.
        uint32_t fi                  = frame_index % Rendering::Renderers::ImGUIRenderer::FRAMES_IN_FLIGHT;
        auto     vb                  = ImguiRenderer->VBHandles[fi];
        auto     ib                  = ImguiRenderer->IdxBHandles[fi];

        CurrentCmdBuf->BeginRenderPass(ImguiRenderer->UIPass, current_framebuffer, true);
        {
            // Direct HOST_VISIBLE writes — one buffer per frame-in-flight, no WAR hazard.
            auto* rrm = Device->RRM ? reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM) : nullptr;
            if (rrm)
            {
                rrm->UpdateBuffer(vb, payload.VertexData.data(), payload.VertexData.size() * sizeof(payload.VertexData[0]));
                rrm->UpdateBuffer(ib, payload.IndexData.data(), payload.IndexData.size() * sizeof(payload.IndexData[0]));
            }

            auto ui_second_cb = Device->CommandBufferMgr->GetCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, Device->SwapchainPtr->CurrentFrame->Index, RenderMainThreadIndex, UICommandBufferIndex, false);
            ui_second_cb->ResetState();
            ui_second_cb->BeginSecondary(ImguiRenderer->UIPass, current_framebuffer);
            ui_second_cb->SetViewport(ImguiRenderer->UIPass->GetRenderAreaWidth(), ImguiRenderer->UIPass->GetRenderAreaHeight());

            ui_second_cb->BindPipeline(Rendering::Specifications::PipelineBindPoint::GRAPHIC, ImguiRenderer->UIPass->Pipeline);

            ui_second_cb->BindVertexBuffer(vb);
            ui_second_cb->BindIndexBuffer(ib, payload.IsIndexBufferUint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

            Rendering::Renderers::PushConstantData pc_data = {};
            pc_data.Scale[0]                               = payload.Pc[0];
            pc_data.Scale[1]                               = payload.Pc[1];

            pc_data.Translate[0]                           = payload.Pc[2];
            pc_data.Translate[1]                           = payload.Pc[3];

            for (uint32_t i = 0; i < payload.DrawDataIndex; ++i)
            {
                const auto& scissor_cmd = payload.ScissorCmds[i];
                const auto& indexed_cmd = payload.IndexedCmds[i];

                ui_second_cb->SetScissor(scissor_cmd.w, scissor_cmd.h, scissor_cmd.x, scissor_cmd.y);
                pc_data.TextureId = payload.TextureIds[i];
                ui_second_cb->PushConstants(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Rendering::Renderers::PushConstantData), &pc_data);
                ui_second_cb->BindDescriptorSets(Device->SwapchainPtr->CurrentFrame->Index);
                ui_second_cb->DrawIndexed(indexed_cmd.IdxCount, indexed_cmd.InstanceCount, indexed_cmd.FirstIndex, indexed_cmd.VertexOffset, indexed_cmd.FirstInstance);
            }

            ui_second_cb->End();

            CurrentCmdBuf->ExecuteSecondaryCommandBuffers(ArrayView<Hardwares::CommandBuffer>{ui_second_cb, 1});
        }

        CurrentCmdBuf->EndRenderPass();
    }

    void AppRenderPipeline::EndOverlayFrame()
    {
        ImguiRenderer->EndFrame();
    }
} // namespace ZEngine::Applications
