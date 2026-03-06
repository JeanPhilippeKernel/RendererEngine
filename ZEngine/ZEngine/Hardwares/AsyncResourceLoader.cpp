#include <AsyncResourceLoader.h>
#include <Helpers/ThreadPool.h>
#include <Rendering/Buffers/Bitmap.h>
#include <VulkanDevice.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/deprecated/stb_image_resize.h>
#include <stb/stb_image_write.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Rendering;

namespace ZEngine::Hardwares
{
    void AsyncResourceLoader::Initialize(VulkanDevice* device)
    {
        Device        = device;
        BufferManager = ZPushStructCtor(Device->Arena, CommandBufferManager);

        BufferManager->Initialize(Device, Device->SwapchainPtr->BufferredFrameCount, 1);
        Helpers::ThreadPoolHelper::Submit([this] { Run(); });
    }

    Textures::TextureHandle AsyncResourceLoader::LoadTextureFile(cstring filename)
    {
        std::unique_lock l(m_mutex_2);

        auto             abs_filename = std::filesystem::absolute(filename).string();

        int              w, h, ch;
        if (!stbi_info(abs_filename.c_str(), &w, &h, &ch))
        {
            return {};
        }

        const std::set<std::string_view>     known_cubmap_file_ext = {".hdr", ".exr"};
        auto                                 file_ext              = std::filesystem::path(filename).extension().string();

        Specifications::TextureSpecification spec{.Width = (uint32_t) w, .Height = (uint32_t) h, .Format = Specifications::ImageFormat::R8G8B8A8_SRGB};

        if (known_cubmap_file_ext.contains(file_ext))
        {
            int face_size   = w / 4;

            spec.IsCubemap  = true;
            spec.LayerCount = 6;
            spec.Format     = Specifications::ImageFormat::R32G32B32A32_SFLOAT;

            spec.Width      = face_size;
            spec.Height     = face_size;
        }

        TextureFileRequest tex_file_req       = {};
        tex_file_req.Filename                 = filename;
        tex_file_req.TextureSpec              = spec;
        tex_file_req.TextureSpec.BytePerPixel = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        tex_file_req.Handle                   = Device->CreateTexture(tex_file_req.TextureSpec);

        m_file_requests.Enqueue(tex_file_req);
        m_cond.notify_one();

        return tex_file_req.Handle;
    }

    void AsyncResourceLoader::Run()
    {
        while (true)
        {
            std::unique_lock l(m_mutex);
            m_cond.wait(l, [this] { return !m_file_requests.Empty() || !m_update_texture_request.Empty() || !m_upload_requests.Empty() || m_cancellation_token.load() == true; });

            if (m_cancellation_token.load() == true)
            {
                break;
            }

            // Processing update requests
            if (m_update_texture_request.Size())
            {
                UpdateTextureRequest tr;
                if (m_update_texture_request.Pop(tr))
                {
                    auto     texture      = Device->GlobalTextures.Access(tr.Handle);
                    auto     img_buf      = Device->Image2DBufferManager.Access(texture->BufferHandle);
                    auto&    spec         = texture->Specification;
                    auto     image_handle = img_buf->GetHandle();
                    uint32_t image_aspect = (texture->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

                    if (Device->HasSeperateTransfertQueueFamily)
                    {
                        Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
                        barrier_spec_0.ImageHandle                                     = image_handle;
                        barrier_spec_0.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                        barrier_spec_0.NewLayout                                       = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                        barrier_spec_0.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
                        barrier_spec_0.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_NONE;
                        barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
                        barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                        barrier_spec_0.LayerCount                                      = spec.LayerCount;
                        barrier_spec_0.SourceQueueFamily                               = Device->TransferFamilyIndex;
                        barrier_spec_0.DestinationQueueFamily                          = Device->GraphicFamilyIndex;
                        Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};
                        auto                           command_buffer_0 = BufferManager->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, Device->SwapchainPtr->CurrentFrame->Index, 0, 2, true);
                        {
                            command_buffer_0.Buffer->TransitionImageLayout(barrier_0);
                            img_buf->Layout = barrier_spec_0.NewLayout;
                        }
                        BufferManager->EndInstantCommandBuffer(command_buffer_0);
                    }

                    VkAccessFlags                                   access_flag  = Device->HasSeperateTransfertQueueFamily ? VK_ACCESS_NONE : VK_ACCESS_TRANSFER_WRITE_BIT;
                    VkPipelineStageFlagBits                         src_stage    = Device->HasSeperateTransfertQueueFamily ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT;

                    Specifications::ImageMemoryBarrierSpecification barrier_spec = {};
                    barrier_spec.ImageHandle                                     = image_handle;
                    barrier_spec.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                    barrier_spec.NewLayout                                       = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                    barrier_spec.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
                    barrier_spec.SourceAccessMask                                = access_flag;
                    barrier_spec.DestinationAccessMask                           = VK_ACCESS_SHADER_READ_BIT;
                    barrier_spec.SourceStageMask                                 = src_stage;
                    barrier_spec.DestinationStageMask                            = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    barrier_spec.LayerCount                                      = spec.LayerCount;
                    barrier_spec.SourceQueueFamily                               = Device->TransferFamilyIndex;
                    barrier_spec.DestinationQueueFamily                          = Device->GraphicFamilyIndex;
                    Primitives::ImageMemoryBarrier barrier{barrier_spec};

                    auto                           command_buffer = BufferManager->GetInstantCommandBuffer(QueueType::GRAPHIC_QUEUE, (Device->SwapchainPtr->CurrentFrame == nullptr ? 0u : Device->SwapchainPtr->CurrentFrame->Index), 0, 2, true);
                    {
                        command_buffer.Buffer->TransitionImageLayout(barrier);
                        img_buf->Layout = barrier_spec.NewLayout;
                    }
                    BufferManager->EndInstantCommandBuffer(command_buffer);

                    Device->TextureHandleToUpdates.Enqueue(tr.Handle);
                }
            }

            // Processing upload requests
            if (m_upload_requests.Size())
            {
                TextureUploadRequest upload_request;
                if (m_upload_requests.Pop(upload_request))
                {
                    auto     texture        = Device->GlobalTextures.Access(upload_request.Handle);
                    auto     img_buf        = Device->Image2DBufferManager.Access(texture->BufferHandle);
                    uint32_t image_aspect   = (texture->Specification.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

                    // todo(jeanphilippekernel): we should weak acquire the frame index
                    auto     command_buffer = BufferManager->GetInstantCommandBuffer(QueueType::TRANSFER_QUEUE, (Device->SwapchainPtr->CurrentFrame == nullptr ? 0u : Device->SwapchainPtr->CurrentFrame->Index), 0, 2, true);
                    {
                        auto                                            image_handle   = img_buf->GetHandle();
                        auto&                                           image_buffer   = img_buf->GetBuffer();

                        Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
                        barrier_spec_0.ImageHandle                                     = image_handle;
                        barrier_spec_0.OldLayout                                       = img_buf->Layout;
                        barrier_spec_0.NewLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
                        barrier_spec_0.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
                        barrier_spec_0.SourceAccessMask                                = 0;
                        barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                        barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_TRANSFER_BIT;
                        barrier_spec_0.LayerCount                                      = upload_request.TextureSpec.LayerCount;
                        barrier_spec_0.SourceQueueFamily                               = Device->TransferFamilyIndex;
                        barrier_spec_0.DestinationQueueFamily                          = Device->TransferFamilyIndex;

                        Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};
                        command_buffer.Buffer->TransitionImageLayout(barrier_0);

                        img_buf->Layout = barrier_spec_0.NewLayout;

                        Device->WriteTextureData(command_buffer.Buffer, upload_request.Handle, upload_request.Buffer.data());
                    }
                    BufferManager->EndInstantCommandBuffer(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT);

                    UpdateTextureRequest tr = {.Handle = upload_request.Handle};

                    m_update_texture_request.Emplace(std::move(tr));
                }
            }

            // Processing file requests
            TextureFileRequest file_request;
            if (m_file_requests.Pop(file_request))
            {
                TextureUploadRequest upload_req = {};

                int                  width = 0, height = 0, channel = 0;
                stbi_set_flip_vertically_on_load(1);

                if (file_request.TextureSpec.IsCubemap)
                {
                    const float* image_data = stbi_loadf(file_request.Filename.data(), &width, &height, &channel, 4);
                    if (!image_data)
                    {
                        ZENGINE_CORE_ERROR("Failed to load texture file : {0}", file_request.Filename.data())
                        continue;
                    }

                    bool               perform_convert_rgb_to_rgba = (channel == STBI_rgb);

                    std::vector<float> output_buffer               = {};
                    if (perform_convert_rgb_to_rgba)
                    {
                        size_t total_pixel = width * height;
                        size_t buffer_size = total_pixel * 4;
                        output_buffer.resize(buffer_size);
                        stbir_resize_float(image_data, width, height, 0, output_buffer.data(), width, height, 0, 4);

                        for (int i = 0; i < total_pixel; ++i)
                        {
                            int offset = i * 4; // RGBA format (4 channels)

                            if (channel == 1)
                            {
                                output_buffer[offset + 3] = 255;
                            }
                            else if (channel == 2)
                            {
                                output_buffer[offset + 3] = image_data[i * 2 + 1];
                            }
                            else if (channel == 3)
                            {
                                output_buffer[offset + 3] = 255;
                            }
                        }
                    }
                    else
                    {
                        size_t total_pixel = width * height;
                        size_t buffer_size = total_pixel * channel;
                        output_buffer.resize(buffer_size);
                        Helpers::secure_memset(output_buffer.data(), 0.f, buffer_size, buffer_size);
                    }

                    stbi_image_free((void*) image_data);

                    Buffers::Bitmap in             = {width, height, 4, Buffers::BitmapFormat::FLOAT, output_buffer.data()};
                    Buffers::Bitmap vertical_cross = Buffers::Bitmap::EquirectangularMapToVerticalCross(in);
                    Buffers::Bitmap cubemap        = Buffers::Bitmap::VerticalCrossToCubemap(vertical_cross);

                    // spec.Width                     = cubemap.Width;
                    // spec.Height                    = cubemap.Height;
                    size_t          buffer_size    = cubemap.Buffer.size();
                    size_t          buffer_byte    = buffer_size * sizeof(uint8_t);
                    upload_req.Buffer.resize(buffer_size);
                    Helpers::secure_memmove(upload_req.Buffer.data(), buffer_byte, cubemap.Buffer.data(), buffer_byte);
                }
                else
                {

                    stbi_uc* image_data = stbi_load(file_request.Filename.data(), &width, &height, &channel, STBI_rgb_alpha);
                    if (!image_data)
                    {
                        ZENGINE_CORE_ERROR("Failed to load texture file : {0}", file_request.Filename.data())
                        continue;
                    }

                    bool perform_convert_rgb_to_rgba = (channel <= STBI_rgb);

                    if (perform_convert_rgb_to_rgba)
                    {
                        size_t total_pixel = width * height;
                        size_t buffer_size = total_pixel * 4;
                        upload_req.Buffer.resize(buffer_size);
                        stbir_resize_uint8(image_data, width, height, 0, upload_req.Buffer.data(), width, height, 0, 4);

                        for (int i = 0; i < total_pixel; ++i)
                        {
                            int offset = i * 4; // RGBA format (4 channels)

                            if (channel == 1)
                            {
                                upload_req.Buffer[offset + 3] = 255;
                            }
                            else if (channel == 2)
                            {
                                upload_req.Buffer[offset + 3] = image_data[i * 2 + 1];
                            }
                            else if (channel == 3)
                            {
                                upload_req.Buffer[offset + 3] = 255;
                            }
                        }
                    }
                    else
                    {
                        size_t total_pixel = width * height;
                        size_t buffer_size = total_pixel * channel;
                        upload_req.Buffer.resize(buffer_size, 0);
                        Helpers::secure_memmove(upload_req.Buffer.data(), buffer_size, image_data, buffer_size);
                    }

                    stbi_image_free(image_data);
                }

                upload_req.BufferSize  = (upload_req.Buffer.size() * sizeof(uint8_t));
                upload_req.Handle      = file_request.Handle;
                upload_req.TextureSpec = file_request.TextureSpec;

                m_upload_requests.Emplace(std::move(upload_req));
            }
        }
    }

    void AsyncResourceLoader::Shutdown()
    {
        {
            std::unique_lock l(m_mutex);
            m_cancellation_token = true;
        }
        m_cond.notify_one();

        BufferManager->Deinitialize();
    }
} // namespace ZEngine::Hardwares
