#pragma once
#include <Camera.h>
#include <Hardwares/VulkanDevice.h>
#include <Helpers/ThreadSafeQueue.h>
#include <ImGUIRenderer.h>
#include <Primitives/Fence.h>
#include <Primitives/Semaphore.h>
#include <RenderPasses/RenderPass.h>
#include <Rendering/Renderers/RenderGraph.h>
#include <Textures/Texture.h>
#include <vulkan/vulkan.h>
#include <span>

namespace ZEngine::Rendering::Renderers
{
    struct ResizeRequest
    {
        uint32_t Width;
        uint32_t Height;
    };

    struct UpdateTextureRequest
    {
        Textures::TextureHandle Handle;
        Textures::Texture*      Texture;
    };

    struct TextureFileRequest
    {
        std::string                          Filename;
        Textures::TextureHandle              Handle;
        Specifications::TextureSpecification TextureSpec;
    };

    struct TextureUploadRequest
    {
        size_t                               BufferSize  = 0;
        Textures::TextureHandle              Handle      = {};
        Specifications::TextureSpecification TextureSpec = {};
        std::vector<uint8_t>                 Buffer      = {};
    };

    struct AsyncResourceLoader;
    struct GraphicRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        cstring                           FrameDepthRenderTargetName   = "g_frame_depth_render_target";
        cstring                           FrameColorRenderTargetName   = "g_frame_color_render_target";

        cstring                           SceneCameraBufferName        = "SceneCamera";
        cstring                           VertexBufferName             = "VertexStorageBuffer";
        cstring                           IndexBufferName              = "IndexStorageBuffer";
        cstring                           TransformBufferName          = "TransformStorageBuffer";
        cstring                           RenderDataBufferName         = "RenderDataStorageBuffer";
        cstring                           MaterialBufferName           = "MaterialStorageBuffer";

        const size_t                      DefaultBufferSize            = ZMega(10);

        Hardwares::UniformBufferSetHandle SceneCameraBufferHandle      = {};
        Textures::TextureHandle           FrameColorRenderTarget       = {};
        Textures::TextureHandle           FrameDepthRenderTarget       = {};

        Scenes::SceneDataPtr              RenderSceneData              = nullptr;

        Hardwares::VulkanDevicePtr        Device                       = nullptr;
        Renderers::ImGUIRendererPtr       ImguiRenderer                = nullptr;
        ZRawPtr(Renderers::RenderGraph) RenderGraph                    = nullptr;
        ZRawPtr(Renderers::AsyncResourceLoader) AsyncLoader            = nullptr;
        Helpers::ThreadSafeQueue<ResizeRequest> EnqueuedResizeRequests = {};

        void                                    Initialize(Hardwares::VulkanDevicePtr device);
        void                                    Deinitialize();
        void                                    DrawScene(Hardwares::CommandBufferPtr const cb, Cameras::Camera* const camera);
        Textures::TextureHandle                 GetFrameOutput();
    };

    struct AsyncResourceLoader
    {
        GraphicRenderer*        Renderer = nullptr;

        void                    Initialize(GraphicRenderer* renderer);
        void                    Run();
        void                    Shutdown();

        Textures::TextureHandle LoadTextureFile(std::string_view filename);

    private:
        std::atomic_bool                               m_cancellation_token{false};
        std::mutex                                     m_mutex;
        std::mutex                                     m_mutex_2;
        std::condition_variable                        m_cond;
        Hardwares::CommandBufferManager                m_buffer_manager{};
        Helpers::ThreadSafeQueue<UpdateTextureRequest> m_update_texture_request;
        Helpers::ThreadSafeQueue<TextureFileRequest>   m_file_requests;
        Helpers::ThreadSafeQueue<TextureUploadRequest> m_upload_requests;
    };
} // namespace ZEngine::Rendering::Renderers
