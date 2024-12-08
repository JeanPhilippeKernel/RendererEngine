#pragma once
#include <Helpers/ThreadSafeQueue.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Rendering/Renderers/ImGUIRenderer.h>
#include <Rendering/Renderers/RenderGraph.h>
#include <Rendering/Renderers/SceneRenderer.h>
#include <Rendering/Swapchain.h>
#include <Textures/Texture.h>
#include <Windows/CoreWindow.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers
{
    enum RenderTarget : uint32_t
    {
        FRAME_OUTPUT = 0,
        ENVIROMENT_CUBEMAP,
        COUNT
    };

    struct RendererInformation
    {
        uint32_t FrameCount{0xFFFFFFFF};
        uint32_t CurrentFrameIndex{0xFFFFFFFF};
        uint64_t SwapchainIdentifier{0xFFFFFFFF};
    };

    struct UpdateTextureRequest;
    struct AsyncResourceLoader;
    struct GraphicRenderer
    {
        static Helpers::Ref<Textures::TextureHandleManager> GlobalTextures;

        static void                               Initialize(const Helpers::Ref<Windows::CoreWindow>& window);
        static void                               Deinitialize();
        static void                               SetViewportSize(uint32_t width, uint32_t height);
        static const RendererInformation&         GetRendererInformation();
        static void                               Update();
        static void                               DrawScene(const Helpers::Ref<Rendering::Cameras::Camera>& camera, const Helpers::Ref<Rendering::Scenes::SceneRawData>& data);
        static void                               BeginImguiFrame();
        static void                               DrawUIFrame();
        static void                               EndImguiFrame();
        static VkDescriptorSet                    GetImguiFrameOutput();
        static void                               BindGlobalTextures(RenderPasses::RenderPass* pass);
        static void                               NewFrame();
        static void                               Present();
        static void                               ResizeSwapchain();
        static Helpers::Ref<Rendering::Swapchain> GetSwapchain();

        static Textures::TextureHandle AddTexture(std::string_view filename);
        static void                    AddTextureToUpdate(UpdateTextureRequest&& req);

    private:
        GraphicRenderer()                       = delete;
        GraphicRenderer(const GraphicRenderer&) = delete;
        ~GraphicRenderer()                      = delete;

    private:
        static RendererInformation                            s_renderer_information;
        static Helpers::Ref<Rendering::Swapchain>             s_swapchain;
        static Helpers::Ref<Buffers::UniformBufferSet>        s_UBCamera;
        static Pools::CommandPool*                            s_command_pool;
        static Buffers::CommandBuffer*                        s_current_command_buffer;
        static Buffers::CommandBuffer*                        s_current_command_buffer_ui;
        static Helpers::Ref<SceneRenderer>                    s_scene_renderer;
        static Helpers::Ref<ImGUIRenderer>                    s_imgui_renderer;
        static Helpers::Scope<RenderGraph>                    s_render_graph;
        static Helpers::Ref<AsyncResourceLoader>              s_resource_loader;
        static Helpers::ThreadSafeQueue<UpdateTextureRequest> s_update_texture_request;
        static Helpers::Ref<Primitives::Fence>                s_transfer_fence;
        static Helpers::Ref<Primitives::Semaphore>            s_transfer_semaphore;
    };

    struct UpdateTextureRequest
    {
        Textures::TextureHandle Handle;
        Textures::TextureRef    Texture;
    };

    struct TextureFileRequest
    {
        std::string             Filename;
        Textures::TextureHandle Handle;
    };

    struct TextureUploadRequest
    {
        Textures::TextureHandle              Handle;
        Specifications::TextureSpecification TextureSpec;
    };

    struct AsyncResourceLoader : public Helpers::RefCounted
    {
        void Initialize();
        void Shutdown();
        void Start();

        void CreateTextureFileRequest(std::string_view file, const Textures::TextureHandle& handle);

    private:
        void Run();

    private:
        std::atomic_bool                               m_running{true};
        std::mutex                                     m_mut;
        Helpers::Ref<Primitives::Fence>                m_transfer_fence;
        Helpers::Ref<Primitives::Semaphore>            m_transfer_semaphore;
        Helpers::Ref<Pools::CommandPool>               m_command_pool;
        Helpers::ThreadSafeQueue<TextureFileRequest>   m_file_requests;
        Helpers::ThreadSafeQueue<TextureUploadRequest> m_upload_requests;
    };
} // namespace ZEngine::Rendering::Renderers
