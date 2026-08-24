#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/GPUTypes.h>
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
        RenderSceneData->LightBuffer      = Device->GpuMem.AllocateBuffer(sizeof(Scenes::LightArrayUBO), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, LightBufferName);

        /*
         * Renderer Passes
         */
        auto scene_depth_prepass          = ZPushStructCtor(Device->Arena, DepthPrePass);
        auto gbuffer_pass                 = ZPushStructCtor(Device->Arena, GbufferPass);
        auto lighting_pass                = ZPushStructCtor(Device->Arena, LightingPass);
        auto skybox_pass                  = ZPushStructCtor(Device->Arena, SkyboxPass);
        auto grid_pass                    = ZPushStructCtor(Device->Arena, GridPass);

        RenderGraph->Initialize(Device, RenderSceneData);

        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);
        // Skybox starts disabled; ApplySkyConfig enables it when a scene with an HDRI sky loads.
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass, false);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);

        RenderGraph->Setup();
        RenderGraph->Compile();

        // Register FrameColor for bindless access now that the graph has allocated it.
        Device->TextureHandleToUpdates.Enqueue(RenderGraph->ResourceInspector->GetRenderTarget(RendererResourceName::FrameColorRenderTargetName));
    }

    void GraphicRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        if (RenderSceneData)
        {
            Device->GpuMem.FreeBuffer(RenderSceneData->TransformBuffer);
            Device->GpuMem.FreeBuffer(RenderSceneData->RenderDataBuffer);
            Device->GpuMem.FreeBuffer(RenderSceneData->MaterialBuffer);
            Device->GpuMem.FreeBuffer(RenderSceneData->LightBuffer);
        }
    }

    void GraphicRenderer::UpdateRMMBindings(Scenes::SceneDataPtr scene)
    {
        if (!scene)
            return;

        // Bind static per-scene buffers once. HOST_VISIBLE — descriptor set once,
        // data written each frame via vmaCopyMemoryToAllocation.
        if (!m_static_buffers_bound && scene->TransformBuffer.Handle)
        {
            auto* depth_pass   = RenderGraph->GetPass("Depth Pre-Pass");
            auto* gbuffer_pass = RenderGraph->GetPass("G-Buffer Pass");
            auto* light_pass   = RenderGraph->GetPass("Lighting Pass");
            if (depth_pass && depth_pass->Handle)
            {
                depth_pass->Handle->SetStorageBuffer("TransformSB", &scene->TransformBuffer);
                depth_pass->Handle->SetStorageBuffer("DrawDataSB", &scene->RenderDataBuffer);
            }
            if (gbuffer_pass && gbuffer_pass->Handle)
            {
                gbuffer_pass->Handle->SetStorageBuffer("TransformSB", &scene->TransformBuffer);
                gbuffer_pass->Handle->SetStorageBuffer("DrawDataSB", &scene->RenderDataBuffer);
                gbuffer_pass->Handle->SetStorageBuffer("MatSB", &scene->MaterialBuffer);
            }
            if (light_pass && light_pass->Handle && scene->LightBuffer.Handle)
                light_pass->Handle->SetStorageBuffer("LightSB", &scene->LightBuffer);
            m_static_buffers_bound = true;
            ZENGINE_CORE_INFO("[GraphicRenderer] Bound TransformSB/DrawDataSB/MatSB/LightSB to passes")
        }

        if (!Device->RRM)
            return;

        auto* rrm = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);

        // Global vertex + index buffers — bind to both geometry passes once ready.
        if (!m_global_buffers_bound && rrm->GlobalBuffersReady())
        {
            const auto* vtx_buf      = rrm->GetGlobalVertexBuffer();
            const auto* idx_buf      = rrm->GetGlobalIndexBuffer();
            auto*       depth_pass   = RenderGraph->GetPass("Depth Pre-Pass");
            auto*       gbuffer_pass = RenderGraph->GetPass("G-Buffer Pass");
            if (depth_pass && depth_pass->Handle)
            {
                depth_pass->Handle->SetStorageBuffer("VertexSB", vtx_buf);
                depth_pass->Handle->SetStorageBuffer("IndexSB", idx_buf);
            }
            if (gbuffer_pass && gbuffer_pass->Handle)
            {
                gbuffer_pass->Handle->SetStorageBuffer("VertexSB", vtx_buf);
                gbuffer_pass->Handle->SetStorageBuffer("IndexSB", idx_buf);
                gbuffer_pass->Handle->UseTextureArray("TextureArray");
            }
            m_global_buffers_bound = true;
            ZENGINE_CORE_INFO("[GraphicRenderer] Bound global VertexSB/IndexSB to geometry passes")
        }
    }

    void GraphicRenderer::DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, Cameras::CameraPtr const camera)
    {
        auto asset_manager   = Managers::AssetManager::Instance();
        auto view_proj       = camera->GetProjection() * camera->GetView();
        auto ubo_camera_data = UBOCameraLayout{.View = camera->GetView(), .Projection = camera->GetProjection(), .Position = Vec4f(camera->GetPosition(), 1.0f), .InvViewProj = view_proj.Inverse()};

        if (Device->RRM && RenderSceneData->MaterialBuffer.Handle)
        {
            auto* rrm = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            rrm->UpdateBuffer(RenderSceneData->MaterialBuffer, asset_manager->GPUMeshMaterials.data(), asset_manager->GPUMeshMaterials.size() * sizeof(asset_manager->GPUMeshMaterials[0]));
        }

        // Light buffer is uploaded by AppRenderPipeline::RenderScene from scene->PendingLights.

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
        if (!sky.IsHDRI())
        {
            RenderGraph->SetPassEnabled("Skybox Pass", false);
            return;
        }

        auto env_path = sky.EnvironmentMap.c_str();
        if (!env_path || env_path[0] == '\0')
        {
            RenderGraph->SetPassEnabled("Skybox Pass", false);
            return;
        }

        auto* vfs = ZEngine::Engine::GetContext() ? ZEngine::Engine::GetContext()->VFS : nullptr;
        if (!vfs)
        {
            ZENGINE_CORE_ERROR("[Renderer] VFS not available — cannot resolve environment map: {}", env_path)
            RenderGraph->SetPassEnabled("Skybox Pass", false);
            return;
        }

        auto path_result   = ZEngine::Core::VFS::VFSPath::FromNative(env_path);
        auto exists_result = path_result.Succeeded() ? vfs->Exists(path_result.Value()) : ZEngine::Core::VFS::VFSResult<bool>::Fail(ZEngine::Core::VFS::VFSError::InvalidPath);
        if (exists_result.Failed() || !exists_result.Value())
        {
            ZENGINE_CORE_ERROR("[Renderer] Environment map not found in VFS: {}", env_path)
            RenderGraph->SetPassEnabled("Skybox Pass", false);
            return;
        }

        auto* pass = RenderGraph->GetPass("Skybox Pass");
        if (pass)
        {
            static_cast<SkyboxPass*>(pass->Callback)->EnvMapPath = env_path;
            pass->Enabled                                        = true;
        }
    }

    void GraphicRenderer::ApplyGridConfig(const Scenes::GridConfig& cfg)
    {
        auto* pass = RenderGraph->GetPass("Grid Pass");
        if (!pass)
            return;
        pass->Enabled = cfg.Enabled;
        if (!cfg.Enabled)
            return;

        auto& p        = static_cast<GridPass*>(pass->Callback)->PushData;
        p.CellSize     = cfg.CellSize;
        p.FadeRadius   = cfg.FadeRadius;
        p.FadeStrength = cfg.FadeStrength;
        p.LineWidth    = cfg.LineWidth;
        p.MaxLOD       = cfg.MaxLOD;
        p.GroundY      = cfg.GroundY;
        secure_memcpy(p.ColorThin, sizeof(p.ColorThin), cfg.ColorThin, sizeof(cfg.ColorThin));
        secure_memcpy(p.ColorThick, sizeof(p.ColorThick), cfg.ColorThick, sizeof(cfg.ColorThick));
        secure_memcpy(p.ColorXAxis, sizeof(p.ColorXAxis), cfg.ColorXAxis, sizeof(cfg.ColorXAxis));
        secure_memcpy(p.ColorZAxis, sizeof(p.ColorZAxis), cfg.ColorZAxis, sizeof(cfg.ColorZAxis));
    }
} // namespace ZEngine::Rendering::Renderers
