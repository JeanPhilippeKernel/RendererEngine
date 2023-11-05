#include <pch.h>
#include <Rendering/Textures/Texture2D.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Core/Coroutine.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

namespace ZEngine::Rendering::Textures
{

    Texture* CreateTexture(const char* path)
    {
        return nullptr;
    }

    Texture* CreateTexture(unsigned int width, unsigned int height)
    {
        return nullptr;
    }

    Texture* CreateTexture(unsigned int width, unsigned int height, float r, float g, float b, float a)
    {
        return nullptr;
    }
} // namespace ZEngine::Rendering::Textures

namespace ZEngine::Rendering::Textures
{
    Ref<Texture2D> Texture2D::Read(std::string_view filename)
    {
        Ref<Texture2D> texture;

        int width = 0, height = 0, channel = 0;
        // stbi_set_flip_vertically_on_load(1);
        stbi_uc* image_data = stbi_load(filename.data(), &width, &height, &channel, STBI_rgb_alpha);

        if (!image_data)
        {
            ZENGINE_CORE_ERROR("Failed to load texture file : %s", filename.data())
            texture = Create(1, 1, 0, 0, 0, 0);
            return texture;
        }

        /*
         * post processing to convert the image from RGB to RBGA
         */
        texture                  = CreateRef<Texture2D>();
        stbi_uc* image_data_rgba = nullptr;
        if (channel == STBI_rgb)
        {
            image_data_rgba = (stbi_uc*) malloc(width * height * 4);
            if (image_data_rgba)
            {
                // Copy RGB data and set alpha channel to 255 (fully opaque)
                for (int i = 0; i < width * height; i++)
                {
                    memcpy(image_data_rgba + i * 4, image_data + i * 3, 3); // Copy RGB channels
                    image_data_rgba[i * 4 + 3] = 255;                       // Alpha channel (fully opaque)
                }
                channel = STBI_rgb_alpha;
                stbi_image_free(image_data);
            }
            else
            {
                ZENGINE_CORE_ERROR("Failed to load texture file : %s", filename.data())
                return texture;
            }
        }
        else
        {
            image_data_rgba = image_data;
        }

        texture->m_byte_per_pixel = channel;
        texture->m_buffer_size    = width * height * channel;
        texture->m_width          = width;
        texture->m_height         = height;

        __CreateVulkanImage(texture, image_data_rgba);

        stbi_image_free(image_data_rgba);
        return texture;
    }

    std::future<Ref<Texture2D>> Texture2D::ReadAsync(std::string_view filename)
    {
        co_return Read(filename);
    }

    Hardwares::BufferImage& Texture2D::GetBuffer()
    {
        return m_image_2d_buffer->GetBuffer();
    }

    const Hardwares::BufferImage& Texture2D::GetBuffer() const
    {
        return m_image_2d_buffer->GetBuffer();
    }

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
    {
        Ref<Texture2D> texture = CreateRef<Texture2D>();

        texture->m_byte_per_pixel  = 4;
        texture->m_buffer_size     = width * height * 4;
        texture->m_width           = width;
        texture->m_height          = height;
        unsigned char image_data[] = {255, 255, 255, 255, '\0'};

        __CreateVulkanImage(texture, image_data);

        return texture;
    }

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height, float r, float g, float b, float a)
    {
        Ref<Texture2D> texture = CreateRef<Texture2D>();

        texture->m_byte_per_pixel = 4;
        texture->m_buffer_size    = width * height * 4;
        texture->m_width          = width;
        texture->m_height         = height;

        unsigned char image_data[] = {0, 0, 0, 0, '\0'};
        image_data[0]              = static_cast<unsigned char>(std::clamp(r, .0f, 255.0f));
        image_data[1]              = static_cast<unsigned char>(std::clamp(g, .0f, 255.0f));
        image_data[2]              = static_cast<unsigned char>(std::clamp(b, .0f, 255.0f));
        image_data[3]              = static_cast<unsigned char>(std::clamp(a, .0f, 255.0f));

        __CreateVulkanImage(texture, image_data);
        return texture;
    }

    Ref<Buffers::Image2DBuffer> Texture2D::GetImage2DBuffer() const
    {
        return m_image_2d_buffer;
    }

    void Texture2D::Dispose()
    {
        m_image_2d_buffer->Dispose();
    }

    void Texture2D::__CreateVulkanImage(Ref<Texture2D>& texture, const void* image_data)
    {
        auto                  device         = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        Hardwares::BufferView staging_buffer = Hardwares::VulkanDevice::CreateBuffer(
            texture->m_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        Hardwares::VulkanDevice::MapAndCopyToMemory(staging_buffer.Memory, texture->m_buffer_size, image_data);

        /* Create VkImage */
        texture->m_image_2d_buffer = CreateRef<Buffers::Image2DBuffer>(
            texture->m_width, texture->m_height, VK_FORMAT_R8G8B8A8_SRGB, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

        /*Transition Image from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL and Copy buffer to image*/
        {
            auto                                            image_handle   = texture->m_image_2d_buffer->GetHandle();
            auto&                                           image_buffer   = texture->m_image_2d_buffer->GetBuffer();
            Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
            barrier_spec_0.ImageHandle                                     = image_handle;
            barrier_spec_0.OldLayout                                       = Specifications::ImageLayout::UNDEFINED;
            barrier_spec_0.NewLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            barrier_spec_0.ImageAspectMask                                 = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier_spec_0.SourceAccessMask                                = 0;
            barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_TRANSFER_BIT;
            Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};

            Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
            barrier_spec_1.ImageHandle                                     = image_handle;
            barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            barrier_spec_1.NewLayout                                       = Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            barrier_spec_1.ImageAspectMask                                 = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier_spec_1.SourceAccessMask                                = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier_spec_1.DestinationAccessMask                           = VK_ACCESS_SHADER_READ_BIT;
            barrier_spec_1.SourceStageMask                                 = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier_spec_1.DestinationStageMask                            = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};

            Hardwares::VulkanDevice::CopyBufferToImage(
                Rendering::QueueType::GRAPHIC_QUEUE,
                staging_buffer,
                image_buffer,
                texture->m_width,
                texture->m_height,
                0,
                std::vector<Primitives::ImageMemoryBarrier>{barrier_0, barrier_1});
        }
        /* Cleanup resource */
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFER, staging_buffer.Handle);
        Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, staging_buffer.Memory);
    }

    Texture2D::~Texture2D()
    {
        Dispose();
    }

} // namespace ZEngine::Rendering::Textures
