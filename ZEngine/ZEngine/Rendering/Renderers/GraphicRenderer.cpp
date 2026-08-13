#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Contracts/RendererDataContract.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/RendererPasses.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Renderers::Contracts;
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
        Device                            = device;
        RenderGraph                       = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);
        RenderSceneData                   = ZPushStructCtor(Device->Arena, Scenes::SceneData);
        /*
         * Shared Buffers
         */
        // All scene buffers are plain HOST_VISIBLE BufferViews — one per buffer type,
        // shared across all frame indices.  Written via RRM::UpdateBuffer each frame.
        RenderSceneData->TransformBuffer  = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, TransformBufferName);
        RenderSceneData->RenderDataBuffer = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, RenderDataBufferName);
        RenderSceneData->MaterialBuffer   = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, MaterialBufferName);

        /*
         * Renderer Passes
         */
        auto     base_pass                = ZPushStructCtor(Device->Arena, BasePass);
        auto     upload_pass              = ZPushStructCtor(Device->Arena, UploadPass);
        auto     scene_depth_prepass      = ZPushStructCtor(Device->Arena, DepthPrePass);
        auto     skybox_pass              = ZPushStructCtor(Device->Arena, SkyboxPass);
        auto     grid_pass                = ZPushStructCtor(Device->Arena, GridPass);

        uint32_t rt_w                     = Device->SwapchainPtr->SwapchainImageWidth;
        uint32_t rt_h                     = Device->SwapchainPtr->SwapchainImageHeight;
        FrameColorRenderTarget            = Device->CreateTexture({.PerformTransition = false, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::R8G8B8A8_UNORM});
        FrameDepthRenderTarget            = Device->CreateTexture({.PerformTransition = false, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE});

        Device->TextureHandleToUpdates.Enqueue(FrameColorRenderTarget);
        /*
         * Render Graph definition
         */
        RenderGraph->Initialize(Device, RenderSceneData);

        RenderGraph->ResourceBuilder->AttachRenderTarget(RendererResourceName::FrameDepthRenderTargetName, FrameDepthRenderTarget);
        RenderGraph->ResourceBuilder->AttachRenderTarget(RendererResourceName::FrameColorRenderTargetName, FrameColorRenderTarget);

        // Skybox starts disabled; ApplySkyConfig enables it when a scene with an HDRI sky loads.
        RenderGraph->AddCallbackPass("Upload Pass", upload_pass);
        RenderGraph->AddCallbackPass("Base Pass", base_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass, false);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);

        RenderGraph->Setup();
        RenderGraph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        Device->GlobalTextures.Remove(FrameColorRenderTarget);
        Device->GlobalTextures.Remove(FrameDepthRenderTarget);
        if (RenderSceneData)
        {
            Device->GpuMem.FreeBuffer(RenderSceneData->TransformBuffer);
            Device->GpuMem.FreeBuffer(RenderSceneData->RenderDataBuffer);
            Device->GpuMem.FreeBuffer(RenderSceneData->MaterialBuffer);
        }
    }

    void GraphicRenderer::UpdateRMMBindings(Scenes::SceneDataPtr scene)
    {
        if (!scene)
            return;

        // Bind static per-scene buffers once (TransformSB, DrawDataSB, MatSB).
        // These are HOST_VISIBLE BufferViews — descriptor binding is set once,
        // data is written directly via vmaCopyMemoryToAllocation each frame.
        if (!m_static_buffers_bound && scene->TransformBuffer.Handle)
        {
            auto& depth_node = RenderGraph->NodeMap["Depth Pre-Pass"];
            auto& base_node  = RenderGraph->NodeMap["Base Pass"];
            if (depth_node.Handle)
            {
                depth_node.Handle->SetInput("TransformSB", &scene->TransformBuffer);
                depth_node.Handle->SetInput("DrawDataSB", &scene->RenderDataBuffer);
            }
            if (base_node.Handle)
            {
                base_node.Handle->SetInput("TransformSB", &scene->TransformBuffer);
                base_node.Handle->SetInput("DrawDataSB", &scene->RenderDataBuffer);
                base_node.Handle->SetInput("MatSB", &scene->MaterialBuffer);
            }
            m_static_buffers_bound = true;
            ZENGINE_CORE_INFO("[GraphicRenderer] Bound TransformSB/DrawDataSB/MatSB to RMM buffers")
        }

        if (!Device->RRM)
            return;

        auto* rrm = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);

        // Global vertex + index buffers — bind once when they become ready.
        // All meshes share these two VkBuffers; per-mesh offsets are in DrawDataSB.
        if (!m_global_buffers_bound && rrm->GlobalBuffersReady())
        {
            const auto* vtx_buf    = rrm->GetGlobalVertexBuffer();
            const auto* idx_buf    = rrm->GetGlobalIndexBuffer();
            auto&       depth_node = RenderGraph->NodeMap["Depth Pre-Pass"];
            auto&       base_node  = RenderGraph->NodeMap["Base Pass"];
            // VertexSB = set 0, binding 1 — IndexSB = set 0, binding 2 (vertex_common.glsl).
            // Use SetInputByBinding to bypass ValidateInput (name lookup inconsistency).
            // Only bind to passes that include vertex_common.glsl (Depth Pre-Pass).
            // Base Pass uses a stub shader with no bindings — skip it.
            if (depth_node.Handle)
            {
                depth_node.Handle->SetInputByBinding(0, 1, vtx_buf);
                depth_node.Handle->SetInputByBinding(0, 2, idx_buf);
            }
            m_global_buffers_bound = true;
            ZENGINE_CORE_INFO("[GraphicRenderer] Bound global VertexSB/IndexSB to packed geometry buffers")
        }
    }

    void GraphicRenderer::DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, Cameras::CameraPtr const camera)
    {
        auto asset_manager   = Managers::AssetManager::Instance();
        auto ubo_camera_data = UBOCameraLayout{.View = camera->GetView(), .Projection = camera->GetProjection(), .Position = Vec4f(camera->GetPosition(), 1.0f)};

        if (Device->RRM && RenderSceneData->MaterialBuffer.Handle)
        {
            auto* rrm = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            rrm->UpdateBuffer(RenderSceneData->MaterialBuffer, asset_manager->GPUMeshMaterials.data(), asset_manager->GPUMeshMaterials.size() * sizeof(asset_manager->GPUMeshMaterials[0]));
        }

        // Push camera data into the per-frame heap; store offset for dynamic descriptor binding
        auto& heap                        = Device->FrameHeaps[Device->SwapchainPtr->CurrentFrame->Index];
        auto  camera_alloc                = heap.Push(&ubo_camera_data, sizeof(UBOCameraLayout), Device->MinUniformBufferOffsetAlignment());
        RenderSceneData->CameraHeapOffset = camera_alloc.Offset;

        RenderGraph->Execute(cb);
    }

    Textures::TextureHandle GraphicRenderer::GetFrameOutput()
    {
        return RenderGraph->ResourceInspector->GetRenderTarget(RendererResourceName::FrameColorRenderTargetName);
    }

    void GraphicRenderer::ApplySkyConfig(const Scenes::SkyConfig& sky)
    {
        auto& node = RenderGraph->NodeMap["Skybox Pass"];

        if (!sky.IsHDRI())
        {
            node.Enabled = false;
            return;
        }

        auto env_path = sky.EnvironmentMap.c_str();
        if (!env_path || env_path[0] == '\0')
        {
            node.Enabled = false;
            return;
        }

        auto* vfs = ZEngine::Engine::GetContext() ? ZEngine::Engine::GetContext()->VFS : nullptr;
        if (!vfs)
        {
            ZENGINE_CORE_ERROR("[Renderer] VFS not available — cannot resolve environment map: {}", env_path)
            node.Enabled = false;
            return;
        }

        auto path_result   = ZEngine::Core::VFS::VFSPath::FromNative(env_path);
        auto exists_result = path_result.Succeeded() ? vfs->Exists(path_result.Value()) : ZEngine::Core::VFS::VFSResult<bool>::Fail(ZEngine::Core::VFS::VFSError::InvalidPath);
        if (exists_result.Failed() || !exists_result.Value())
        {
            ZENGINE_CORE_ERROR("[Renderer] Environment map not found in VFS: {}", env_path)
            node.Enabled = false;
            return;
        }

        auto* skybox_pass       = static_cast<SkyboxPass*>(node.CallbackPass);
        skybox_pass->EnvMapPath = env_path;
        node.Enabled            = true;
    }
} // namespace ZEngine::Rendering::Renderers
