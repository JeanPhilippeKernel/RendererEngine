#include <pch.h>
#include <Editor.h>
#include <RenderLayer.h>
#include <ZEngine/Core/CoreEvent.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Windows;
using namespace ZEngine::Core;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Rendering::Meshes;
using namespace Tetragrama::Controllers;

namespace Tetragrama::Layers
{

    RenderLayer::RenderLayer(const char* name) : Layer(name) {}

    RenderLayer::~RenderLayer() {}

    void RenderLayer::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena)
    {
        arena->CreateSubArena(ZMega(30), &LocalArena);

        RenderableNodes.init(arena, 5000);
    }

    void RenderLayer::Deinitialize() {}

    void RenderLayer::Update(TimeStep dt)
    {
        if (!ParentContext)
        {
            return;
        }

        auto ctx = reinterpret_cast<EditorContext*>(ParentContext);
        // ctx->CurrentScenePtr->RenderScene->ComputeAllTransforms();
        ctx->CameraControllerPtr->Update(dt);
    }

    bool RenderLayer::OnEvent(CoreEvent& e)
    {
        if (ParentContext)
        {
            auto ctx = reinterpret_cast<EditorContext*>(ParentContext);
            ctx->CameraControllerPtr->OnEvent(e);
        }
        return false;
    }

    void RenderLayer::Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer)
    {
        if (!ParentContext)
        {
            return;
        }

        auto  ctx                  = reinterpret_cast<EditorContext*>(ParentContext);
        auto  camera               = ctx->CameraControllerPtr->GetCamera();
        auto  device               = renderer->Device;
        auto  gpu_scene_data       = renderer->RenderSceneData;

        auto  vtx_buffer_set       = device->StorageBufferSetManager.Access(gpu_scene_data->VertexBufferHandle);
        auto  idx_buffer_set       = device->StorageBufferSetManager.Access(gpu_scene_data->IndexBufferHandle);
        auto  transform_buffer_set = device->StorageBufferSetManager.Access(gpu_scene_data->TransformBufferHandle);
        auto  rd_buffer_set        = device->StorageBufferSetManager.Access(gpu_scene_data->RenderDataBufferHandle);
        auto  material_buffer_set  = device->StorageBufferSetManager.Access(gpu_scene_data->MaterialBufferHandle);
        auto  indirect_buffer_set  = device->IndirectBufferSetManager.Access(gpu_scene_data->IndirectBufferHandle);

        auto  vtx_buffer           = vtx_buffer_set->At(device->CurrentFrameIndex);
        auto  idx_buffer           = idx_buffer_set->At(device->CurrentFrameIndex);
        auto  transform_buffer     = transform_buffer_set->At(device->CurrentFrameIndex);
        auto  rd_buffer            = rd_buffer_set->At(device->CurrentFrameIndex);
        auto  indirect_buffer      = indirect_buffer_set->At(device->CurrentFrameIndex);
        auto  material_buffer      = material_buffer_set->At(device->CurrentFrameIndex);

        auto  current_scene        = ctx->CurrentScenePtr;
        auto& suballocs            = current_scene->NodeSubMeshesAllocations;

        if (current_scene->TransformBufferDirty[device->CurrentFrameIndex].load(std::memory_order_acquire) || current_scene->MeshAllocationDirty[device->CurrentFrameIndex].load(std::memory_order_acquire))
        {

            if (current_scene->TransformBufferDirty[device->CurrentFrameIndex].exchange(false, std::memory_order_acquire))
            {
                auto transform_data_view = ArrayView{current_scene->GlobalTransforms};
                transform_buffer->UploadRange(transform_data_view.data(), 0, transform_data_view.size_bytes());
            }

            if (current_scene->MeshAllocationDirty[device->CurrentFrameIndex].exchange(false, std::memory_order_acquire))
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

                auto vertex_data_view       = ArrayView{current_scene->Vertices};
                auto index_data_view        = ArrayView{current_scene->Indices};
                auto material_data_view     = ArrayView{ctx->AssetManagerPtr->GPUMeshMaterials};

                auto sub_mesh_alloc_view    = ArrayView{SubMeshAllocations};
                auto indirect_commands_view = ArrayView{DrawIndirectCommands};
                vtx_buffer->UploadRange(vertex_data_view.data(), 0, vertex_data_view.size_bytes());
                idx_buffer->UploadRange(index_data_view.data(), 0, index_data_view.size_bytes());
                material_buffer->UploadRange(material_data_view.data(), 0, material_data_view.size_bytes());

                rd_buffer->UploadRange(sub_mesh_alloc_view.data(), 0, sub_mesh_alloc_view.size_bytes());

                indirect_buffer->UploadRange(indirect_commands_view.data(), 0, indirect_commands_view.size_bytes());
                indirect_buffer->CommandCount = indirect_commands_view.size_bytes() / sizeof(VkDrawIndirectCommand);

                ZReleaseScratch(scratch);
            }
        }

        // Todo (Kernel) : When we'll start considering multithreaded support
        // we might want to renderer->EnqueueAsync({command_buffer, {camera, frame_data} })
        renderer->DrawScene(command_buffer, camera, nullptr);
    }
} // namespace Tetragrama::Layers
