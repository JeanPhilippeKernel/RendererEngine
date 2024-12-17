#pragma once
#include <Buffers/IndexBuffer.h>
#include <Buffers/VertexBuffer.h>
#include <Camera.h>
#include <Hardwares/VulkanDevice.h>
#include <Helpers/ThreadSafeQueue.h>
#include <Primitives/Fence.h>
#include <Primitives/Semaphore.h>
#include <RenderPasses/RenderPass.h>
#include <Rendering/Buffers/CommandBuffer.h>
#include <Rendering/Renderers/RenderGraph.h>
#include <Textures/Texture.h>
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

    struct BufferSet;
    struct AsyncResourceLoader;
    struct ImGUIRenderer;
    struct SceneRenderer;
    struct GraphicRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        Hardwares::VulkanDevice*                     Device         = nullptr;
        Helpers::Ref<Textures::TextureHandleManager> GlobalTextures = nullptr;
        Helpers::Ref<SceneRenderer>                  SceneRenderer  = nullptr;
        Helpers::Ref<ImGUIRenderer>                  ImguiRenderer  = nullptr;
        Helpers::Scope<RenderGraph>                  RenderGraph    = nullptr;

        void            Initialize(Hardwares::VulkanDevice* device);
        void            Deinitialize();
        void            SetViewportSize(uint32_t width, uint32_t height);
        void            Update();
        void            DrawScene(Buffers::CommandBuffer* const command_buffer, const Helpers::Ref<Cameras::Camera>& camera, const Helpers::Ref<Scenes::SceneRawData>& data);
        VkDescriptorSet GetImguiFrameOutput();
        void            BindGlobalTextures(RenderPasses::RenderPass* pass);

        Helpers::Ref<Buffers::VertexBufferSet>   CreateVertexBufferSet();
        Helpers::Ref<Buffers::StorageBufferSet>  CreateStorageBufferSet();
        Helpers::Ref<Buffers::IndirectBufferSet> CreateIndirectBufferSet();
        Helpers::Ref<Buffers::IndexBufferSet>    CreateIndexBufferSet();
        Helpers::Ref<Buffers::UniformBufferSet>  CreateUniformBufferSet();

        Helpers::Ref<RenderPasses::RenderPass> CreateRenderPass(const Specifications::RenderPassSpecification& spec);
        Helpers::Ref<Textures::Texture>        CreateTexture(const Specifications::TextureSpecification& spec);
        Helpers::Ref<Textures::Texture>        CreateTexture(uint32_t width, uint32_t height);
        Helpers::Ref<Textures::Texture>        CreateTexture(uint32_t width, uint32_t height, float r, float g, float b, float a);
        Textures::TextureHandle                AddTexture(std::string_view filename);
        void                                   AddTextureToUpdate(UpdateTextureRequest&& req);

    private:
        Helpers::Ref<Buffers::UniformBufferSet>        m_UBCamera;
        Helpers::Ref<AsyncResourceLoader>              m_resource_loader;
        Helpers::ThreadSafeQueue<UpdateTextureRequest> m_update_texture_request;
        Helpers::Ref<Primitives::Fence>                m_transfer_fence;
        Helpers::Ref<Primitives::Semaphore>            m_transfer_semaphore;
    };

    struct AsyncResourceLoader : public Helpers::RefCounted
    {
        GraphicRenderer* Renderer = nullptr;

        void Initialize(GraphicRenderer* renderer);
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
        Helpers::ThreadSafeQueue<TextureFileRequest>   m_file_requests;
        Helpers::ThreadSafeQueue<TextureUploadRequest> m_upload_requests;
    };
} // namespace ZEngine::Rendering::Renderers
