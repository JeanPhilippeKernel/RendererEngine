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
#include <span>

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
        size_t                               BufferSize;
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

        Hardwares::VulkanDevice*                              Device                   = nullptr;
        Helpers::Ref<SceneRenderer>                           SceneRenderer            = nullptr;
        Helpers::Ref<ImGUIRenderer>                           ImguiRenderer            = nullptr;
        Helpers::Scope<RenderGraph>                           RenderGraph              = nullptr;
        Helpers::HandleManager<Buffers::VertexBufferSetRef>   VertexBufferSetManager   = {300};
        Helpers::HandleManager<Buffers::StorageBufferSetRef>  StorageBufferSetManager  = {300};
        Helpers::HandleManager<Buffers::IndirectBufferSetRef> IndirectBufferSetManager = {300};
        Helpers::HandleManager<Buffers::IndexBufferSetRef>    IndexBufferSetManager    = {300};
        Helpers::HandleManager<Buffers::UniformBufferSetRef>  UniformBufferSetManager  = {300};

        void            Initialize(Hardwares::VulkanDevice* device);
        void            Deinitialize();
        void            SetViewportSize(uint32_t width, uint32_t height);
        void            Update();
        void            DrawScene(Buffers::CommandBuffer* const command_buffer, const Helpers::Ref<Cameras::Camera>& camera, const Helpers::Ref<Scenes::SceneRawData>& data);
        void            WriteDescriptorSets(std::span<Hardwares::WriteDescriptorSetRequest> requests);
        VkDescriptorSet GetImguiFrameOutput();
        void            BindGlobalTextures(RenderPasses::RenderPass* pass);

        Buffers::VertexBufferSetHandle   CreateVertexBufferSet();
        Buffers::StorageBufferSetHandle  CreateStorageBufferSet();
        Buffers::IndirectBufferSetHandle CreateIndirectBufferSet();
        Buffers::IndexBufferSetHandle    CreateIndexBufferSet();
        Buffers::UniformBufferSetHandle  CreateUniformBufferSet();

        Helpers::Ref<RenderPasses::RenderPass> CreateRenderPass(const Specifications::RenderPassSpecification& spec);
        Helpers::Ref<Textures::Texture>        CreateTexture(const Specifications::TextureSpecification& spec);
        Helpers::Ref<Textures::Texture>        CreateTexture(uint32_t width, uint32_t height);
        Helpers::Ref<Textures::Texture>        CreateTexture(uint32_t width, uint32_t height, float r, float g, float b, float a);
        Textures::TextureHandle                LoadTextureFile(std::string_view filename);

    private:
        Buffers::UniformBufferSetHandle   m_scene_camera_buffer_handle;
        Helpers::Ref<AsyncResourceLoader> m_resource_loader;
    };

    struct AsyncResourceLoader : public Helpers::RefCounted
    {
        GraphicRenderer* Renderer = nullptr;

        void Initialize(GraphicRenderer* renderer);
        void Shutdown();
        void Start();

        void EnqueueTextureRequest(std::string_view file, const Textures::TextureHandle& handle);

    private:
        void Run();

    private:
        std::atomic_bool                               m_cancellation_token{false};
        std::mutex                                     m_mutex;
        std::condition_variable                        m_cond;
        std::vector<uint8_t>                           m_temp_buffer{};
        Helpers::ThreadSafeQueue<UpdateTextureRequest> m_update_texture_request;
        Helpers::ThreadSafeQueue<TextureFileRequest>   m_file_requests;
        Helpers::ThreadSafeQueue<TextureUploadRequest> m_upload_requests;
    };
} // namespace ZEngine::Rendering::Renderers
