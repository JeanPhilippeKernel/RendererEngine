#include <AppRenderPipeline.h>
#include <Core/Containers/Array.h>

using namespace ZEngine::Core::Containers;

namespace ZEngine::Applications
{
    void AppRenderPipeline::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device        = device;
        SceneRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::GraphicRenderer);
        ImguiRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::ImGUIRenderer);
        Device->Arena->CreateSubArena(ZMega(30), &LocalArena);

        SceneRenderer->Initialize(Device);
        ImguiRenderer->Initialize(Device);
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
        Device->NewFrame();
        CurrentCmdBuf = Device->GetCommandBuffer();
    }

    void AppRenderPipeline::EndFrame()
    {
        Device->EnqueueCommandBuffer(CurrentCmdBuf);
        Device->Present();
    }

    void AppRenderPipeline::RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene)
    {
        if (scene->TransformBufferDirty[Device->CurrentFrameIndex].load(std::memory_order_acquire) || scene->MeshAllocationDirty[Device->CurrentFrameIndex].load(std::memory_order_acquire))
        {
            auto  gpu_scene_data       = SceneRenderer->RenderSceneData;

            auto  vtx_buffer_set       = Device->StorageBufferSetManager.Access(gpu_scene_data->VertexBufferHandle);
            auto  idx_buffer_set       = Device->StorageBufferSetManager.Access(gpu_scene_data->IndexBufferHandle);
            auto  transform_buffer_set = Device->StorageBufferSetManager.Access(gpu_scene_data->TransformBufferHandle);
            auto  rd_buffer_set        = Device->StorageBufferSetManager.Access(gpu_scene_data->RenderDataBufferHandle);

            auto  indirect_buffer_set  = Device->IndirectBufferSetManager.Access(gpu_scene_data->IndirectBufferHandle);

            auto  vtx_buffer           = vtx_buffer_set->At(Device->CurrentFrameIndex);
            auto  idx_buffer           = idx_buffer_set->At(Device->CurrentFrameIndex);
            auto  transform_buffer     = transform_buffer_set->At(Device->CurrentFrameIndex);
            auto  rd_buffer            = rd_buffer_set->At(Device->CurrentFrameIndex);
            auto  indirect_buffer      = indirect_buffer_set->At(Device->CurrentFrameIndex);

            auto& suballocs            = scene->NodeSubMeshesAllocations;

            if (scene->TransformBufferDirty[Device->CurrentFrameIndex].exchange(false, std::memory_order_acquire))
            {
                auto transform_data_view = ArrayView{scene->GlobalTransforms};
                transform_buffer->Write(transform_data_view);
            }

            if (scene->MeshAllocationDirty[Device->CurrentFrameIndex].exchange(false, std::memory_order_acquire))
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

                vtx_buffer->Write(vertex_data_view);
                idx_buffer->Write(index_data_view);

                rd_buffer->Write(sub_mesh_alloc_view);

                indirect_buffer->Write(indirect_commands_view);

                ZReleaseScratch(scratch);
            }
        }

        // Todo (Kernel) : When we'll start considering multithreaded support
        // we might want to renderer->EnqueueAsync({command_buffer, {camera, frame_data} })
        SceneRenderer->DrawScene(CurrentCmdBuf, camera);
    }

    void AppRenderPipeline::BeginOverlayFrame()
    {
        ImguiRenderer->NewFrame();
    }

    void AppRenderPipeline::EndOverlayFrame()
    {
        ImguiRenderer->DrawFrame(CurrentCmdBuf);
    }
} // namespace ZEngine::Applications
