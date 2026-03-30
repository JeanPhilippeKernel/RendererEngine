#pragma once
#include <Helpers/ThreadSafeQueue.h>
#include <Rendering/Primitives/Semaphore.h>
#include <Textures/Texture.h>
#include <ZEngineDef.h>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
    struct AsyncGPUOperation;
    struct BufferView;
    struct CommandBuffer;

    struct TextureFileRequest
    {
        std::string                                     Filename;
        Rendering::Textures::TextureHandle              Handle;
        Rendering::Specifications::TextureSpecification TextureSpec;
        uint8_t                                         FrameIdx  = 0;
        uint8_t                                         ThreadIdx = 0;
    };

    struct TextureUploadRequest
    {
        uint8_t                                         FrameIdx    = 0;
        uint8_t                                         ThreadIdx   = 0;
        size_t                                          BufferSize  = 0;
        Rendering::Textures::TextureHandle              Handle      = {};
        Rendering::Specifications::TextureSpecification TextureSpec = {};
        std::vector<uint8_t>                            Buffer      = {};
    };

    struct AsyncResourceLoader
    {
        enum class UploadType : uint8_t
        {
            TEXTURE_BUFFER = 0,
            TEXTURE_FILE,
            BUFFER,
            STAGING_BUFFER,
            BUFFER_CLEAR,
        };

        struct UploadRequest
        {
            UploadType UploadType;
            union
            {
                struct
                {
                    unsigned char*                     Data      = nullptr;
                    cstring                            Filename  = nullptr;
                    Rendering::Textures::TextureHandle TexHandle = {};
                } TextureUpload;
                struct
                {
                    BufferView* const Buffer     = nullptr;
                    const void*       Data       = nullptr;
                    uint32_t          Offset     = 0;
                    uint32_t          ClearValue = 0;
                    size_t            ByteSize   = 0;
                } BufferUpload;
            };
        };

        struct TimelineJob
        {
            CommandBuffer*                    Buffer      = nullptr;
            Rendering::Primitives::Semaphore* Timeline    = nullptr;
            uint32_t                          WaitFlag    = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            uint64_t                          SignalValue = 0;
        };

        struct DeferralUpload
        {
            UploadType                         UploadType;
            uint8_t                            FrameIdx  = 0;
            uint8_t                            ThreadIdx = 0;
            unsigned char*                     Data      = nullptr;
            Rendering::Textures::TextureHandle TexHandle = {};
        };

        std::atomic_uint64_t                                       NextValue             = 1;
        std::atomic_uint32_t                                       Counter               = 0;
        Rendering::Primitives::Semaphore*                          Timeline              = nullptr;
        VulkanDevice*                                              Device                = nullptr;
        Core::Containers::Array<Core::Containers::Array<uint64_t>> RetireValues          = {};
        Helpers::ThreadSafeQueue<TimelineJob>                      AsyncTimelineJobQueue = {};
        Helpers::ThreadSafeQueue<DeferralUpload>                   DeferralUploadQueue   = {};

        void                                                       Initialize(VulkanDevice* device);

        void                                                       UploadTextureBuffer(uint8_t frame_index, uint8_t thread_index, const Rendering::Textures::TextureHandle& handle, unsigned char* data);
        void                                                       UploadBuffer(uint8_t frame_index, uint8_t thread_index, BufferView* const buffer, const void* data, uint32_t offset, size_t byte_size);
        void                                                       UploadFromStagingBuffer(uint8_t frame_index, uint8_t thread_index, BufferView* const destination, const void* data, uint32_t offset, size_t byte_size);
        void                                                       ClearBuffer(uint8_t frame_index, uint8_t thread_index, BufferView* const buffer, uint32_t offset, size_t byte_size, uint32_t clear_value);
        Rendering::Textures::TextureHandle                         LoadTextureFile(cstring filename) = delete;

        void                                                       Submit(UploadType type, uint8_t frame_index, uint8_t thread_index, const UploadRequest& request);
        void                                                       SubmitDeferral(DeferralUpload&& deferral);
        Rendering::Textures::TextureHandle                         Submit(uint8_t frame_index, uint8_t thread_index, const UploadRequest& request);

        void                                                       CompleteDeferrals();
        void                                                       SubmitAsyncJobs();
        void                                                       ResetCommandBuffers(uint8_t frame_index, uint8_t thread_index);

        void                                                       Run();
        void                                                       Shutdown();

    private:
        std::atomic_bool                               m_pump_running{false};
        std::atomic_bool                               m_cancellation_token{false};
        std::mutex                                     m_mutex;
        Helpers::ThreadSafeQueue<TextureFileRequest>   m_file_requests;
        Helpers::ThreadSafeQueue<TextureUploadRequest> m_upload_requests;
    };
    ZDEFINE_PTR(AsyncResourceLoader);
} // namespace ZEngine::Hardwares
