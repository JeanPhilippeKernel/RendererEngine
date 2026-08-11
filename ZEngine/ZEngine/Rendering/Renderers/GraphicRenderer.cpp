#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
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
        Device                                  = device;
        RenderGraph                             = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);
        RenderSceneData                         = ZPushStructCtor(Device->Arena, Scenes::SceneData);
        /*
         * Shared Buffers
         */
        RenderSceneData->VertexBufferHandle     = Device->CreateStorageBufferSet();
        RenderSceneData->IndexBufferHandle      = Device->CreateStorageBufferSet();
        RenderSceneData->TransformBufferHandle  = Device->CreateStorageBufferSet();
        RenderSceneData->RenderDataBufferHandle = Device->CreateStorageBufferSet();
        RenderSceneData->MaterialBufferHandle   = Device->CreateStorageBufferSet();

        auto vtx_buffer_set                     = Device->StorageBufferSetManager.Access(RenderSceneData->VertexBufferHandle);
        auto idx_buffer_set                     = Device->StorageBufferSetManager.Access(RenderSceneData->IndexBufferHandle);
        auto tranform_buffer_set                = Device->StorageBufferSetManager.Access(RenderSceneData->TransformBufferHandle);
        auto rd_buffer_set                      = Device->StorageBufferSetManager.Access(RenderSceneData->RenderDataBufferHandle);
        auto material_buffer_set                = Device->StorageBufferSetManager.Access(RenderSceneData->MaterialBufferHandle);

        for (int i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
        {
            vtx_buffer_set->At(i)->Allocate(DefaultBufferSize, VertexBufferName);
            idx_buffer_set->At(i)->Allocate(DefaultBufferSize, IndexBufferName);
            tranform_buffer_set->At(i)->Allocate(DefaultBufferSize, TransformBufferName);
            rd_buffer_set->At(i)->Allocate(DefaultBufferSize, RenderDataBufferName);
            material_buffer_set->At(i)->Allocate(DefaultBufferSize, MaterialBufferName);
        }

        /*
         * Renderer Passes
         */
        auto     base_pass           = ZPushStructCtor(Device->Arena, BasePass);
        auto     upload_pass         = ZPushStructCtor(Device->Arena, UploadPass);
        auto     scene_depth_prepass = ZPushStructCtor(Device->Arena, DepthPrePass);
        auto     skybox_pass         = ZPushStructCtor(Device->Arena, SkyboxPass);
        auto     grid_pass           = ZPushStructCtor(Device->Arena, GridPass);
        auto     gbuffer_pass        = ZPushStructCtor(Device->Arena, GbufferPass);
        auto     lighting_pass       = ZPushStructCtor(Device->Arena, LightingPass);
        // auto composite_pass      = ZPushStructCtor(Device->Arena, CompositePass);

        uint32_t rt_w                = Device->SwapchainPtr->SwapchainImageWidth;
        uint32_t rt_h                = Device->SwapchainPtr->SwapchainImageHeight;
        FrameColorRenderTarget       = Device->CreateTexture({.PerformTransition = false, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::R8G8B8A8_UNORM});
        FrameDepthRenderTarget       = Device->CreateTexture({.PerformTransition = false, .Width = rt_w, .Height = rt_h, .Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE});

        Device->TextureHandleToUpdates.Enqueue(FrameColorRenderTarget);
        /*
         * Render Graph definition
         */
        RenderGraph->Initialize(Device, RenderSceneData);

        // RenderGraph->ResourceBuilder->AttachRenderTarget(RendererResourceName::FrameSharedRenderTargetName, FrameSharedRenderTarget);
        RenderGraph->ResourceBuilder->AttachRenderTarget(RendererResourceName::FrameDepthRenderTargetName, FrameDepthRenderTarget);
        RenderGraph->ResourceBuilder->AttachRenderTarget(RendererResourceName::FrameColorRenderTargetName, FrameColorRenderTarget);

        RenderGraph->ResourceBuilder->CreateBufferSet("g_scene_directional_light_buffer");
        RenderGraph->ResourceBuilder->CreateBufferSet("g_scene_point_light_buffer");
        RenderGraph->ResourceBuilder->CreateBufferSet("g_scene_spot_light_buffer");

        // Skybox starts disabled; ApplySkyConfig enables it when a scene with an HDRI sky loads.
        RenderGraph->AddCallbackPass("Upload Pass", upload_pass);
        RenderGraph->AddCallbackPass("Base Pass", base_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass, false);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);
        //  RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        //      RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);

        RenderGraph->Setup();
        RenderGraph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        Device->GlobalTextures.Remove(FrameColorRenderTarget);
        Device->GlobalTextures.Remove(FrameDepthRenderTarget);
    }

    void GraphicRenderer::DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, Cameras::CameraPtr const camera)
    {
        auto asset_manager       = Managers::AssetManager::Instance();
        auto ubo_camera_data     = UBOCameraLayout{.View = camera->GetView(), .Projection = camera->GetProjection(), .Position = Vec4f(camera->GetPosition(), 1.0f)};

        auto material_buffer_set = Device->StorageBufferSetManager.Access(RenderSceneData->MaterialBufferHandle);
        auto material_buffer     = material_buffer_set->At(Device->SwapchainPtr->CurrentFrame->Index);

        material_buffer->Write(frame_index, thread_index, ArrayView{asset_manager->GPUMeshMaterials});

        // Push camera data into the per-frame heap; store offset for dynamic descriptor binding
        auto& heap                        = Device->FrameHeaps[Device->SwapchainPtr->CurrentFrame->Index];
        auto  camera_alloc                = heap.Push(&ubo_camera_data, sizeof(UBOCameraLayout), Device->MinUniformBufferOffsetAlignment());
        RenderSceneData->CameraHeapOffset = camera_alloc.Offset;

        // todo : expand F, T to the render graph
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
