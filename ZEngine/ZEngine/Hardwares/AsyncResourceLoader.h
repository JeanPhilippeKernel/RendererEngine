#pragma once
#include <Helpers/ThreadSafeQueue.h>
#include <Textures/Texture.h>
#include <ZEngineDef.h>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
    struct CommandBufferManager;

    struct UpdateTextureRequest
    {
        Rendering::Textures::TextureHandle Handle;
        Rendering::Textures::Texture*      Texture;
    };

    struct TextureFileRequest
    {
        std::string                                     Filename;
        Rendering::Textures::TextureHandle              Handle;
        Rendering::Specifications::TextureSpecification TextureSpec;
    };

    struct TextureUploadRequest
    {
        size_t                                          BufferSize  = 0;
        Rendering::Textures::TextureHandle              Handle      = {};
        Rendering::Specifications::TextureSpecification TextureSpec = {};
        std::vector<uint8_t>                            Buffer      = {};
    };

    struct AsyncResourceLoader
    {
        VulkanDevice*                      Device        = nullptr;
        Hardwares::CommandBufferManager*   BufferManager = nullptr;

        void                               Initialize(VulkanDevice* renderer);
        void                               Run();
        void                               Shutdown();

        Rendering::Textures::TextureHandle LoadTextureFile(cstring filename);

    private:
        std::atomic_bool                               m_cancellation_token{false};
        std::mutex                                     m_mutex;
        std::mutex                                     m_mutex_2;
        std::condition_variable                        m_cond;
        Helpers::ThreadSafeQueue<UpdateTextureRequest> m_update_texture_request;
        Helpers::ThreadSafeQueue<TextureFileRequest>   m_file_requests;
        Helpers::ThreadSafeQueue<TextureUploadRequest> m_upload_requests;
    };
    ZDEFINE_PTR(AsyncResourceLoader);
} // namespace ZEngine::Hardwares
