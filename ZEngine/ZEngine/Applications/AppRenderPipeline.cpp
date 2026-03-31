#include <AppRenderPipeline.h>
#include <Core/Containers/Array.h>
#include <Rendering/Specifications/FormatSpecification.h>

using namespace ZEngine::Core::Containers;

namespace ZEngine::Applications
{
    void AppRenderPipeline::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device                  = device;
        RenderWorkerThreadCount = Device->CommandBufferMgr->TotalThreadCount - 1u;
        UICommandBufferIndex    = RenderMainThreadIndex + 1u;
        Device->Arena->CreateSubArena(ZMega(30), &LocalArena);

        SceneRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::GraphicRenderer);
        ImguiRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::ImGUIRenderer);

        SceneRenderer->Initialize(Device);
        ImguiRenderer->Initialize(Device);

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

    void AppRenderPipeline::BeginFrame()
    {
        auto swapchain = Device->SwapchainPtr;

        swapchain->AcquireNextImage(CurrentMailBoxBufferHead);

        for (uint8_t thread_idx = 0; thread_idx < Device->CommandBufferMgr->TotalThreadCount; ++thread_idx)
        {
            Device->CommandBufferMgr->ResetPool(swapchain->CurrentFrame->Index, thread_idx);
        }

        Device->AsyncResLoader->CompleteDeferrals();

        // uint8_t render_worker_thread_idx = RenderThreadIndex + 1;
        // for (uint8_t worker_thread_idx = 0; worker_thread_idx < RenderWorkerThreadCount; ++worker_thread_idx)
        // {
        //     auto thread_idx                             = render_worker_thread_idx + worker_thread_idx;
        // }
        CurrentCmdBuf = Device->CommandBufferMgr->GetCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, swapchain->CurrentFrame->Index, RenderMainThreadIndex, 0, true);
    }

    void AppRenderPipeline::EndFrame()
    {
        Device->CommandBufferMgr->EnqueueBuffer(CurrentCmdBuf);
        Device->SwapchainPtr->Present();
    }

    void AppRenderPipeline::RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene)
    {
        auto swpachain    = Device->SwapchainPtr;
        auto frame_index  = swpachain->CurrentFrame->Index;
        auto thread_index = RenderMainThreadIndex;

        if (scene->TransformBufferDirty[Device->SwapchainPtr->CurrentFrame->Index].load(std::memory_order_acquire) || scene->MeshAllocationDirty[Device->SwapchainPtr->CurrentFrame->Index].load(std::memory_order_acquire))
        {
            auto  gpu_scene_data       = SceneRenderer->RenderSceneData;

            auto  vtx_buffer_set       = Device->StorageBufferSetManager.Access(gpu_scene_data->VertexBufferHandle);
            auto  idx_buffer_set       = Device->StorageBufferSetManager.Access(gpu_scene_data->IndexBufferHandle);
            auto  transform_buffer_set = Device->StorageBufferSetManager.Access(gpu_scene_data->TransformBufferHandle);
            auto  rd_buffer_set        = Device->StorageBufferSetManager.Access(gpu_scene_data->RenderDataBufferHandle);

            auto  indirect_buffer_set  = Device->IndirectBufferSetManager.Access(gpu_scene_data->IndirectBufferHandle);

            auto  vtx_buffer           = vtx_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);
            auto  idx_buffer           = idx_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);
            auto  transform_buffer     = transform_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);
            auto  rd_buffer            = rd_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);
            auto  indirect_buffer      = indirect_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);

            auto& suballocs            = scene->NodeSubMeshesAllocations;

            if (scene->TransformBufferDirty[Device->SwapchainPtr->CurrentFrame->Index].exchange(false, std::memory_order_acquire))
            {
                auto transform_data_view = ArrayView{scene->GlobalTransforms};
                transform_buffer->Write(frame_index, thread_index, transform_data_view);
            }

            if (scene->MeshAllocationDirty[Device->SwapchainPtr->CurrentFrame->Index].exchange(false, std::memory_order_acquire))
            {
                auto                                                                            scratch              = ZGetScratch(&LocalArena);

                ZEngine::Core::Containers::Array<ZEngine::Rendering::Meshes::SubMeshAllocation> SubMeshAllocations   = {};
                ZEngine::Core::Containers::Array<VkDrawIndirectCommand>                         DrawIndirectCommands = {};
                SubMeshAllocations.init(scratch.Arena, suballocs.size());

                for (const auto& [_, alloc] : suballocs)
                {
                    SubMeshAllocations.push(alloc);
                }

                DrawIndirectCommands.init(scratch.Arena, SubMeshAllocations.size());
                for (unsigned i = 0; i < SubMeshAllocations.size(); ++i)
                {
                    DrawIndirectCommands.push({
                        .vertexCount   = SubMeshAllocations[i].IndexCount,
                        .instanceCount = SubMeshAllocations[i].InstanceCount,
                        .firstVertex   = 0,
                        .firstInstance = i,
                    });
                }

                auto vertex_data_view       = ArrayView{scene->Vertices};
                auto index_data_view        = ArrayView{scene->Indices};

                auto sub_mesh_alloc_view    = ArrayView{SubMeshAllocations};
                auto indirect_commands_view = ArrayView{DrawIndirectCommands};

                vtx_buffer->Write(frame_index, thread_index, vertex_data_view);
                idx_buffer->Write(frame_index, thread_index, index_data_view);

                rd_buffer->Write(frame_index, thread_index, sub_mesh_alloc_view);

                indirect_buffer->Write(frame_index, thread_index, indirect_commands_view);

                ZReleaseScratch(scratch);
            }
        }

        // Todo (Kernel) : When we'll start considering multithreaded support
        // we might want to renderer->EnqueueAsync({command_buffer, {camera, frame_data} })
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

        auto swpachain           = Device->SwapchainPtr;
        auto frame_index         = swpachain->CurrentFrame->Index;
        auto thread_index        = RenderMainThreadIndex;

        auto current_framebuffer = Device->SwapchainPtr->SwapchainFramebuffers[Device->SwapchainPtr->CurrentFrame->ImageIndex];

        CurrentCmdBuf->BeginRenderPass(ImguiRenderer->UIPass, current_framebuffer, true);
        {
            auto vtx_data_view     = ArrayView{payload.VertexData.data(), payload.VertexData.size()};
            auto idx_data_view     = ArrayView{payload.IndexData.data(), payload.IndexData.size()};

            auto vertex_buffer_set = Device->VertexBufferSetManager.Access(payload.VBHandle);
            auto index_buffer_set  = Device->IndexBufferSetManager.Access(payload.IdxBHandle);

            auto vertex_buffer     = vertex_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);
            auto index_buffer      = index_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);

            vertex_buffer->Write(frame_index, thread_index, vtx_data_view);
            index_buffer->Write(frame_index, thread_index, idx_data_view);

            auto ui_second_cb = Device->CommandBufferMgr->GetCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE, Device->SwapchainPtr->CurrentFrame->Index, RenderMainThreadIndex, UICommandBufferIndex, false);
            ui_second_cb->ResetState();
            ui_second_cb->BeginSecondary(ImguiRenderer->UIPass, current_framebuffer);
            ui_second_cb->SetViewport(ImguiRenderer->UIPass->GetRenderAreaWidth(), ImguiRenderer->UIPass->GetRenderAreaHeight());

            ui_second_cb->BindPipeline(Rendering::Specifications::PipelineBindPoint::GRAPHIC, ImguiRenderer->UIPass->Pipeline);

            ui_second_cb->BindVertexBuffer(*vertex_buffer);
            ui_second_cb->BindIndexBuffer(*index_buffer, payload.IsIndexBufferUint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

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
