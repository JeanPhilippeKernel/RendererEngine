#include <ZEngine/Engine.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Compute/FrustumCullingPass.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/DepthPrePass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/SkyboxPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>

using namespace ZEngine::Hardwares;
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
        Device          = device;
        RenderGraph     = ZPushStructCtorArgs(Device->Arena, Renderers::RenderGraph);
        RenderSceneData = ZPushStructCtor(Device->Arena, Scenes::SceneData);
        ZENGINE_VALIDATE_ASSERT(Device->SwapchainPtr->BufferredFrameCount <= Scenes::SceneData::MAX_FRAMES_IN_FLIGHT, "SceneData buffers must cover every buffered frame")
        constexpr const char*  transform_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]       = {"TransformStorageBuffer[0]", "TransformStorageBuffer[1]", "TransformStorageBuffer[2]"};
        constexpr const char*  render_data_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]     = {"RenderDataStorageBuffer[0]", "RenderDataStorageBuffer[1]", "RenderDataStorageBuffer[2]"};
        constexpr const char*  material_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]        = {"MaterialStorageBuffer[0]", "MaterialStorageBuffer[1]", "MaterialStorageBuffer[2]"};
        constexpr const char*  light_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]           = {"LightStorageBuffer[0]", "LightStorageBuffer[1]", "LightStorageBuffer[2]"};
        constexpr const char*  culling_input_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT]   = {"FrustumCullingInput[0]", "FrustumCullingInput[1]", "FrustumCullingInput[2]"};
        constexpr const char*  culled_indirect_names[Scenes::SceneData::MAX_FRAMES_IN_FLIGHT] = {"FrustumCulledIndirect[0]", "FrustumCulledIndirect[1]", "FrustumCulledIndirect[2]"};
        constexpr VkDeviceSize culling_input_size                                             = Scenes::SceneData::MAX_DRAW_COMMANDS * sizeof(Scenes::FrustumCullingInput);
        constexpr VkDeviceSize culled_indirect_size                                           = Scenes::SceneData::MAX_DRAW_COMMANDS * sizeof(VkDrawIndirectCommand);
        for (uint32_t i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
        {
            RenderSceneData->TransformBuffers[i]      = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, transform_names[i]);
            RenderSceneData->RenderDataBuffers[i]     = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, render_data_names[i]);
            RenderSceneData->MaterialBuffers[i]       = Device->GpuMem.AllocateBuffer(DefaultBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, material_names[i]);
            RenderSceneData->LightBuffers[i]          = Device->GpuMem.AllocateBuffer(sizeof(Scenes::LightArrayUBO), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, light_names[i]);
            RenderSceneData->CullingInputBuffers[i]   = Device->GpuMem.AllocateBuffer(culling_input_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, Core::Memory::GpuMemoryDomain::HostUniform, culling_input_names[i]);
            RenderSceneData->CulledIndirectBuffers[i] = Device->GpuMem.AllocateBuffer(culled_indirect_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, Core::Memory::GpuMemoryDomain::DeviceGeometry, culled_indirect_names[i]);
        }

        /*
         * Renderer Passes
         */
        auto scene_depth_prepass  = ZPushStructCtor(Device->Arena, DepthPrePass);
        auto frustum_culling_pass = ZPushStructCtor(Device->Arena, FrustumCullingPass);
        auto gbuffer_pass         = ZPushStructCtor(Device->Arena, GbufferPass);
        auto lighting_pass        = ZPushStructCtor(Device->Arena, LightingPass);
        auto skybox_pass          = ZPushStructCtor(Device->Arena, SkyboxPass);
        auto grid_pass            = ZPushStructCtor(Device->Arena, GridPass);

        RenderGraph->Initialize(Device, RenderSceneData);
        RenderGraph->ImportBuffer(RendererBufferName::Transform, &RenderSceneData->TransformBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::RenderData, &RenderSceneData->RenderDataBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::Material, &RenderSceneData->MaterialBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::Light, &RenderSceneData->LightBuffers[0]);
        // Setup needs one valid imported view to establish declarations. DrawScene
        // updates both pointers to the active frame's views before Execute(),
        // which is when RenderGraph stamps its runtime barriers.
        RenderGraph->ImportBuffer(RendererBufferName::CullingInput, &RenderSceneData->CullingInputBuffers[0]);
        RenderGraph->ImportBuffer(RendererBufferName::CulledIndirect, &RenderSceneData->CulledIndirectBuffers[0]);

        RenderGraph->AddCallbackPass("Frustum Culling Pass", frustum_culling_pass);
        RenderGraph->AddCallbackPass("Depth Pre-Pass", scene_depth_prepass);
        RenderGraph->AddCallbackPass("G-Buffer Pass", gbuffer_pass);
        RenderGraph->AddCallbackPass("Lighting Pass", lighting_pass);
        // Skybox starts disabled; ApplySkyConfig enables it when a scene with an HDRI sky loads.
        RenderGraph->AddCallbackPass("Skybox Pass", skybox_pass, false);
        RenderGraph->AddCallbackPass("Grid Pass", grid_pass);

        RenderGraph->Setup();
        RenderGraph->Compile();

        if (auto* culling_pass = RenderGraph->GetPass("Frustum Culling Pass"); culling_pass && culling_pass->Handle)
        {
            auto* compute_pass = static_cast<RenderPasses::ComputePass*>(culling_pass->Handle);
            for (uint32_t i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
            {
                compute_pass->SetStorageBufferForFrame("CullingInputSB", i, &RenderSceneData->CullingInputBuffers[i]);
                compute_pass->SetStorageBufferForFrame("CulledIndirectSB", i, &RenderSceneData->CulledIndirectBuffers[i]);
            }
        }

        // Register FrameColor for bindless access now that the graph has allocated it.
        Device->TextureHandleToUpdates.Enqueue(RenderGraph->ResourceInspector->GetRenderTarget(RendererResourceName::FrameColorRenderTargetName));
    }

    void GraphicRenderer::Deinitialize()
    {
        RenderGraph->Dispose();
        if (RenderSceneData)
        {
            for (uint32_t i = 0; i < Device->SwapchainPtr->BufferredFrameCount; ++i)
            {
                Device->GpuMem.FreeBuffer(RenderSceneData->TransformBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->RenderDataBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->MaterialBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->LightBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->CullingInputBuffers[i]);
                Device->GpuMem.FreeBuffer(RenderSceneData->CulledIndirectBuffers[i]);
            }
        }
    }

    void GraphicRenderer::UpdateRMMBindings(Scenes::SceneDataPtr scene)
    {
        if (!scene)
            return;

        // The buffers are per-frame, so each descriptor-set frame receives its own
        // physical buffer. The bindings themselves are immutable after setup.
        if (!m_scene_buffers_bound && scene->TransformBuffers[0].Handle)
        {
            auto* depth_pass   = RenderGraph->GetPass("Depth Pre-Pass");
            auto* gbuffer_pass = RenderGraph->GetPass("G-Buffer Pass");
            auto* light_pass   = RenderGraph->GetPass("Lighting Pass");
            for (uint32_t frame_index = 0; frame_index < Device->SwapchainPtr->BufferredFrameCount; ++frame_index)
            {
                if (depth_pass && depth_pass->Handle)
                {
                    auto* gp = static_cast<RenderPasses::GraphicPass*>(depth_pass->Handle);
                    gp->SetStorageBufferForFrame("TransformSB", frame_index, &scene->TransformBuffers[frame_index]);
                    gp->SetStorageBufferForFrame("DrawDataSB", frame_index, &scene->RenderDataBuffers[frame_index]);
                }
                if (gbuffer_pass && gbuffer_pass->Handle)
                {
                    auto* gp = static_cast<RenderPasses::GraphicPass*>(gbuffer_pass->Handle);
                    gp->SetStorageBufferForFrame("TransformSB", frame_index, &scene->TransformBuffers[frame_index]);
                    gp->SetStorageBufferForFrame("DrawDataSB", frame_index, &scene->RenderDataBuffers[frame_index]);
                    gp->SetStorageBufferForFrame("MatSB", frame_index, &scene->MaterialBuffers[frame_index]);
                }
                if (light_pass && light_pass->Handle)
                    static_cast<RenderPasses::GraphicPass*>(light_pass->Handle)->SetStorageBufferForFrame("LightSB", frame_index, &scene->LightBuffers[frame_index]);
            }
            m_scene_buffers_bound = true;
            ZENGINE_CORE_INFO("[GraphicRenderer] Bound per-frame TransformSB/DrawDataSB/MatSB/LightSB descriptors")
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
                auto* gp = static_cast<RenderPasses::GraphicPass*>(depth_pass->Handle);
                gp->SetStorageBuffer("VertexSB", vtx_buf);
                gp->SetStorageBuffer("IndexSB", idx_buf);
            }
            if (gbuffer_pass && gbuffer_pass->Handle)
            {
                auto* gp = static_cast<RenderPasses::GraphicPass*>(gbuffer_pass->Handle);
                gp->SetStorageBuffer("VertexSB", vtx_buf);
                gp->SetStorageBuffer("IndexSB", idx_buf);
                gp->UseTextureArray("TextureArray");
            }
            m_global_buffers_bound = true;
            ZENGINE_CORE_INFO("[GraphicRenderer] Bound global VertexSB/IndexSB to geometry passes")
        }
    }

    Hardwares::CommandBuffer* GraphicRenderer::DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, Cameras::CameraPtr const camera)
    {
        ZENGINE_VALIDATE_ASSERT(frame_index < Scenes::SceneData::MAX_FRAMES_IN_FLIGHT, "Invalid scene-buffer frame index")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Transform, &RenderSceneData->TransformBuffers[frame_index]), "Transform buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::RenderData, &RenderSceneData->RenderDataBuffers[frame_index]), "Render data buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Material, &RenderSceneData->MaterialBuffers[frame_index]), "Material buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::Light, &RenderSceneData->LightBuffers[frame_index]), "Light buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::CullingInput, &RenderSceneData->CullingInputBuffers[frame_index]), "Culling input buffer is not imported into the render graph")
        ZENGINE_VALIDATE_ASSERT(RenderGraph->UpdateImportedBuffer(RendererBufferName::CulledIndirect, &RenderSceneData->CulledIndirectBuffers[frame_index]), "Culled indirect buffer is not imported into the render graph")

        auto asset_manager   = Managers::AssetManager::Instance();
        auto view_proj       = camera->GetProjection() * camera->GetView();
        auto ubo_camera_data = UBOCameraLayout{.View = camera->GetView(), .Projection = camera->GetProjection(), .Position = Vec4f(camera->GetPosition(), 1.0f), .InvViewProj = view_proj.Inverse()};

        if (Device->RRM && RenderSceneData->MaterialBuffers[frame_index].Handle)
        {
            auto* rrm = reinterpret_cast<Rendering::RenderResourceManager*>(Device->RRM);
            rrm->UpdateBuffer(RenderSceneData->MaterialBuffers[frame_index], asset_manager->GPUMeshMaterials.data(), asset_manager->GPUMeshMaterials.size() * sizeof(asset_manager->GPUMeshMaterials[0]));
        }

        // Light buffer is uploaded by AppRenderPipeline::RenderScene from scene->PendingLights.

        // Push camera data into the per-frame heap; store offset for dynamic descriptor binding
        auto& heap                        = Device->FrameHeaps[Device->SwapchainPtr->CurrentFrame->Index];
        auto  camera_alloc                = heap.Push(&ubo_camera_data, sizeof(UBOCameraLayout), Device->MinUniformBufferOffsetAlignment());
        RenderSceneData->CameraHeapOffset = camera_alloc.Offset;

        return RenderGraph->Execute(cb);
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
            if (!static_cast<SkyboxPass*>(pass->Callback)->ConfigureEnvironmentMap(env_path))
            {
                RenderGraph->SetPassEnabled("Skybox Pass", false);
                return;
            }
            RenderGraph->SetPassEnabled("Skybox Pass", true);
        }
    }

    void GraphicRenderer::ApplyGridConfig(const Scenes::GridConfig& cfg)
    {
        auto* pass = RenderGraph->GetPass("Grid Pass");
        if (!pass)
            return;
        RenderGraph->SetPassEnabled("Grid Pass", cfg.Enabled);
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
