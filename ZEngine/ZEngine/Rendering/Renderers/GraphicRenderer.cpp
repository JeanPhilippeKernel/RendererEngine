#include <pch.h>
#include <RendererPasses.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Specifications/FormatSpecification.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Renderers::Contracts;
using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;

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
        SceneCameraBufferHandle                 = Device->CreateUniformBufferSet();

        RenderSceneData->VertexBufferHandle     = Device->CreateStorageBufferSet();
        RenderSceneData->IndexBufferHandle      = Device->CreateStorageBufferSet();
        RenderSceneData->TransformBufferHandle  = Device->CreateStorageBufferSet();
        RenderSceneData->RenderDataBufferHandle = Device->CreateStorageBufferSet();
        RenderSceneData->MaterialBufferHandle   = Device->CreateStorageBufferSet();
        RenderSceneData->IndirectBufferHandle   = Device->CreateIndirectBufferSet();

        auto scene_camera                       = Device->UniformBufferSetManager.Access(SceneCameraBufferHandle);

        auto vtx_buffer_set                     = Device->StorageBufferSetManager.Access(RenderSceneData->VertexBufferHandle);
        auto idx_buffer_set                     = Device->StorageBufferSetManager.Access(RenderSceneData->IndexBufferHandle);
        auto tranform_buffer_set                = Device->StorageBufferSetManager.Access(RenderSceneData->TransformBufferHandle);
        auto rd_buffer_set                      = Device->StorageBufferSetManager.Access(RenderSceneData->RenderDataBufferHandle);
        auto material_buffer_set                = Device->StorageBufferSetManager.Access(RenderSceneData->MaterialBufferHandle);
        auto indirect_buffer_set                = Device->IndirectBufferSetManager.Access(RenderSceneData->IndirectBufferHandle);

        for (int i = 0; i < Device->SwapchainImageCount; ++i)
        {
            scene_camera->At(i)->Allocate(sizeof(UBOCameraLayout), SceneCameraBufferName);

            vtx_buffer_set->At(i)->Allocate(DefaultBufferSize, VertexBufferName);
            idx_buffer_set->At(i)->Allocate(DefaultBufferSize, IndexBufferName);
            tranform_buffer_set->At(i)->Allocate(DefaultBufferSize, TransformBufferName);
            rd_buffer_set->At(i)->Allocate(DefaultBufferSize, RenderDataBufferName);
            material_buffer_set->At(i)->Allocate(DefaultBufferSize, MaterialBufferName);
            indirect_buffer_set->At(i)->Allocate(DefaultBufferSize, "indirectbuffer");
        }

        /*
         * Renderer Passes
         */
        auto initial_pass        = ZPushStructCtor(Device->Arena, InitialPass);
        auto scene_depth_prepass = ZPushStructCtor(Device->Arena, DepthPrePass);
        auto skybox_pass         = ZPushStructCtor(Device->Arena, SkyboxPass);
        auto grid_pass           = ZPushStructCtor(Device->Arena, GridPass);
        auto gbuffer_pass        = ZPushStructCtor(Device->Arena, GbufferPass);
        auto lighting_pass       = ZPushStructCtor(Device->Arena, LightingPass);

        FrameColorRenderTarget   = Device->CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::R8G8B8A8_UNORM});
        FrameDepthRenderTarget   = Device->CreateTexture({.PerformTransition = false, .Width = 1280, .Height = 780, .Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE});

        Device->TextureHandleToUpdates.Enqueue(FrameColorRenderTarget);
        /*
         * Subsystems initialization
         */
        RenderGraph->Initialize(Device->Arena, this);
        /*
         * Render Graph definition
         */
        RenderGraph->Builder->AttachRenderTarget(FrameDepthRenderTargetName, FrameDepthRenderTarget);
        RenderGraph->Builder->AttachRenderTarget(FrameColorRenderTargetName, FrameColorRenderTarget);

        RenderGraph->Builder->CreateBufferSet("g_scene_directional_light_buffer");
        RenderGraph->Builder->CreateBufferSet("g_scene_point_light_buffer");
        RenderGraph->Builder->CreateBufferSet("g_scene_spot_light_buffer");

        RenderGraph->AddCallbackPass("Initial Pass", initial_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        // RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        //  RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);

        RenderGraph->Setup();
        RenderGraph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        Device->GlobalTextures.Remove(FrameColorRenderTarget);
        Device->GlobalTextures.Remove(FrameDepthRenderTarget);
    }

    void GraphicRenderer::DrawScene(Hardwares::CommandBufferPtr const cb, Cameras::Camera* const camera)
    {
        auto ubo_camera_data = UBOCameraLayout{.View = camera->GetViewMatrix(), .Projection = camera->GetPerspectiveMatrix(), .Position = glm::vec4(camera->GetPosition(), 1.0f)};

        auto buffer_set      = Device->UniformBufferSetManager.Access(SceneCameraBufferHandle);
        auto camera_buf      = buffer_set->At(Device->CurrentFrameIndex);

        camera_buf->Write(reinterpret_cast<void*>(&ubo_camera_data), sizeof(UBOCameraLayout));

        RenderGraph->Execute(cb, RenderSceneData);
    }

    Textures::TextureHandle GraphicRenderer::GetFrameOutput()
    {
        return RenderGraph->GetRenderTarget(FrameColorRenderTargetName);
    }
} // namespace ZEngine::Rendering::Renderers
