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
        Textures::TextureRef    Texture;
    };

    struct TextureFileRequest
    {
        std::string             Filename;
        Textures::TextureHandle Handle;
    };

    struct TextureUploadRequest
    {
        size_t                               BufferSize;
        Textures::TextureHandle              Handle;
        Specifications::TextureSpecification TextureSpec;
    };

    struct AsyncResourceLoader;
    struct GraphicRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        const std::string_view                  FrameDepthRenderTargetName = "g_frame_depth_render_target";
        const std::string_view                  FrameColorRenderTargetName = "g_frame_color_render_target";
        Hardwares::UniformBufferSetHandle       SceneCameraBufferHandle    = {};
        Textures::TextureHandle                 FrameColorRenderTarget     = {};
        Textures::TextureHandle                 FrameDepthRenderTarget     = {};
        Hardwares::VulkanDevice*                Device                     = nullptr;
        Helpers::Ref<ImGUIRenderer>             ImguiRenderer              = nullptr;
        Helpers::Scope<RenderGraph>             RenderGraph                = nullptr;
        Helpers::Ref<AsyncResourceLoader>       AsyncLoader                = nullptr;
        Helpers::ThreadSafeQueue<ResizeRequest> EnqueuedResizeRequests     = {};

        void                                    Initialize(Hardwares::VulkanDevice* device);
        void                                    Deinitialize();
        void                                    Update();
        void                                    DrawScene(Hardwares::CommandBuffer* const command_buffer, Cameras::Camera* const camera, Scenes::SceneRawData* const scene);
        VkDescriptorSet                         GetImguiFrameOutput();

        Helpers::Ref<RenderPasses::RenderPass>  CreateRenderPass(const Specifications::RenderPassSpecification& spec);
        Helpers::Ref<Textures::Texture>         CreateTexture(const Specifications::TextureSpecification& spec);
        Helpers::Ref<Textures::Texture>         CreateTexture(uint32_t width, uint32_t height);
        Helpers::Ref<Textures::Texture>         CreateTexture(uint32_t width, uint32_t height, float r, float g, float b, float a);

    private:
    };

    struct AsyncResourceLoader : public Helpers::RefCounted
    {
        GraphicRenderer*        Renderer = nullptr;

        void                    Initialize(GraphicRenderer* renderer);
        void                    Run();
        void                    Shutdown();

        void                    EnqueueTextureRequest(std::string_view file, const Textures::TextureHandle& handle);
        Textures::TextureHandle LoadTextureFile(std::string_view filename);
        Textures::TextureHandle LoadTextureFileSync(std::string_view filename);

    private:
        std::atomic_bool                               m_cancellation_token{false};
        std::mutex                                     m_mutex;
        std::condition_variable                        m_cond;
        std::vector<uint8_t>                           m_temp_buffer{};
        Hardwares::CommandBufferManager                m_buffer_manager{};
        Helpers::ThreadSafeQueue<UpdateTextureRequest> m_update_texture_request;
        Helpers::ThreadSafeQueue<TextureFileRequest>   m_file_requests;
        Helpers::ThreadSafeQueue<TextureUploadRequest> m_upload_requests;
    };
} // namespace ZEngine::Rendering::Renderers
