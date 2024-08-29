#include <pch.h>
#include <Core/Coroutine.h>
#include <Rendering/Textures/Texture2D.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Primitives/ImageMemoryBarrier.h>
#include <Rendering/Buffers/Bitmap.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef __GNUC__
#define STBI_NO_SIMD
#endif
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <stb/stb_image_resize.h>

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
        int width = 0, height = 0, channel = 0;
        // stbi_set_flip_vertically_on_load(1);
        stbi_uc* image_data = stbi_load(filename.data(), &width, &height, &channel, STBI_rgb_alpha);

        if (!image_data)
        {
            ZENGINE_CORE_ERROR("Failed to load texture file : {0}", filename.data())
            return Create(1, 1, 0, 0, 0, 0);
        }
        /*
         * post processing to convert the image from RGB to RBGA
         */
        channel = (channel == STBI_rgb) ? STBI_rgb_alpha : channel;
        std::vector<uint8_t> output_buffer(width * height * channel);
        stbir_resize_uint8(image_data, width, height, 0, output_buffer.data(), width, height, 0, channel);
        stbi_image_free(image_data);

        Specifications::TextureSpecification spec = {};
        spec.Width                                = width;
        spec.Height                               = height;
        spec.Format                               = Specifications::ImageFormat::R8G8B8A8_SRGB;
        spec.BytePerPixel                         = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        spec.Data                                 = output_buffer.data();
        auto texture                              = Create(spec);

        return texture;
    }

    Ref<Texture2D> Texture2D::ReadCubemap(std::string_view filename)
    {
        stbi_set_flip_vertically_on_load(1);

        int          width = 0, height = 0, channel = 0;
        const float* image_data = stbi_loadf(filename.data(), &width, &height, &channel, 4);
        /*
         * post processing to convert the image from RGB to RBGA
         */
        channel = (channel == STBI_rgb) ? STBI_rgb_alpha : channel;
        std::vector<float> output_buffer(width * height * channel);
        stbir_resize_float(image_data, width, height, 0, output_buffer.data(), width, height, 0, channel);
        stbi_image_free((void*) image_data);

        Buffers::Bitmap in             = {width, height, 4, Buffers::BitmapFormat::FLOAT, output_buffer.data()};
        Buffers::Bitmap vertical_cross = Buffers::Bitmap::EquirectangularMapToVerticalCross(in);
        Buffers::Bitmap cubemap        = Buffers::Bitmap::VerticalCrossToCubemap(vertical_cross);

        Specifications::TextureSpecification cubemap_texture_spec = {};
        cubemap_texture_spec.IsCubemap                            = true;
        cubemap_texture_spec.Width                                = cubemap.Width;
        cubemap_texture_spec.Height                               = cubemap.Height;
        cubemap_texture_spec.Format                               = Specifications::ImageFormat::R32G32B32A32_SFLOAT;
        cubemap_texture_spec.BytePerPixel                         = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(cubemap_texture_spec.Format)];
        cubemap_texture_spec.Data                                 = cubemap.Buffer.data();
        cubemap_texture_spec.LayerCount                           = 6;

        return Create(cubemap_texture_spec);
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

    Ref<Texture2D> Texture2D::Create(const Specifications::TextureSpecification& spec)
    {
        Ref<Texture2D> texture = CreateRef<Texture2D>();
        texture->m_specification = spec;
        FillAsVulkanImage(texture, spec);
        return texture;
    }

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
    {
        unsigned char image_data[] = {255, 255, 255, 255, '\0'};

        Specifications::TextureSpecification spec = {};
        spec.Width                                = width;
        spec.Height                               = height;
        spec.Format                               = Specifications::ImageFormat::R8G8B8A8_SRGB;
        spec.BytePerPixel                         = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        spec.Data                                 = image_data;
        return Create(spec);
    }

    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height, float r, float g, float b, float a)
    {
        unsigned char image_data[]                = {0, 0, 0, 0, '\0'};
        image_data[0]                             = static_cast<unsigned char>(std::clamp(r, .0f, 255.0f));
        image_data[1]                             = static_cast<unsigned char>(std::clamp(g, .0f, 255.0f));
        image_data[2]                             = static_cast<unsigned char>(std::clamp(b, .0f, 255.0f));
        image_data[3]                             = static_cast<unsigned char>(std::clamp(a, .0f, 255.0f));
        Specifications::TextureSpecification spec = {};
        spec.Width                                = width;
        spec.Height                               = height;
        spec.Format                               = Specifications::ImageFormat::R8G8B8A8_SRGB;
        spec.BytePerPixel                         = Specifications::BytePerChannelMap[VALUE_FROM_SPEC_MAP(spec.Format)];
        spec.Data                                 = image_data;
        return Create(spec);
    }

    Ref<Buffers::Image2DBuffer> Texture2D::GetImage2DBuffer() const
    {
        return m_image_2d_buffer;
    }

    void Texture2D::Dispose()
    {
        m_image_2d_buffer->Dispose();
    }

    void Texture2D::FillAsVulkanImage(Ref<Texture2D>& texture, const Specifications::TextureSpecification& spec)
    {
        texture->m_byte_per_pixel = spec.BytePerPixel;
        texture->m_buffer_size    = spec.Width * spec.Height * spec.BytePerPixel * spec.LayerCount;
        texture->m_width          = spec.Width;
        texture->m_height         = spec.Height;
        texture->m_is_depth       = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE);

        auto                  device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        Hardwares::BufferView staging_buffer =
            Hardwares::VulkanDevice::CreateBuffer(texture->m_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        Hardwares::VulkanDevice::MapAndCopyToMemory(staging_buffer, texture->m_buffer_size, spec.Data);

        /* Create VkImage */
        uint32_t storage_bit   = spec.IsUsageStorage ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
        uint32_t transfert_bit = spec.IsUsageTransfert ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
        uint32_t sampled_bit   = spec.IsUsageSampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
        uint32_t image_aspect  = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t image_usage_attachment =
            (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkFormat image_format      = (spec.Format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE) ? Hardwares::VulkanDevice::FindDepthFormat()
                                                                                                             : Specifications::ImageFormatMap[static_cast<uint32_t>(spec.Format)];
        texture->m_image_2d_buffer = CreateRef<Buffers::Image2DBuffer>(Specifications::Image2DBufferSpecification(
            texture->m_width,
            texture->m_height,
            spec.IsCubemap ? Specifications::ImageType::Cubemap : Specifications::ImageType::Flat2D,
            image_format,
            VkImageUsageFlagBits(image_usage_attachment | transfert_bit | sampled_bit | storage_bit),
            VkImageAspectFlagBits(image_aspect),
            spec.LayerCount,
            spec.IsCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0));

        if (spec.PerformTransition)
        {
            /*Transition Image from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL OR VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL and Copy buffer to
             * image*/
            auto                                            image_handle   = texture->m_image_2d_buffer->GetHandle();
            auto&                                           image_buffer   = texture->m_image_2d_buffer->GetBuffer();
            Specifications::ImageMemoryBarrierSpecification barrier_spec_0 = {};
            barrier_spec_0.ImageHandle                                     = image_handle;
            barrier_spec_0.OldLayout                                       = Specifications::ImageLayout::UNDEFINED;
            barrier_spec_0.NewLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            barrier_spec_0.ImageAspectMask                                 = VkImageAspectFlagBits(image_aspect);
            barrier_spec_0.SourceAccessMask                                = 0;
            barrier_spec_0.DestinationAccessMask                           = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier_spec_0.SourceStageMask                                 = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            barrier_spec_0.DestinationStageMask                            = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier_spec_0.LayerCount                                      = spec.LayerCount;
            Primitives::ImageMemoryBarrier barrier_0{barrier_spec_0};

            Specifications::ImageMemoryBarrierSpecification barrier_spec_1 = {};
            barrier_spec_1.ImageHandle                                     = image_handle;
            barrier_spec_1.OldLayout                                       = Specifications::ImageLayout::TRANSFER_DST_OPTIMAL;
            barrier_spec_1.NewLayout             = VkImageAspectFlagBits(image_aspect) == VK_IMAGE_ASPECT_DEPTH_BIT ? Specifications::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                                                                                    : Specifications::ImageLayout::SHADER_READ_ONLY_OPTIMAL;
            barrier_spec_1.ImageAspectMask       = VkImageAspectFlagBits(image_aspect);
            barrier_spec_1.SourceAccessMask      = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier_spec_1.DestinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier_spec_1.SourceStageMask       = VK_PIPELINE_STAGE_TRANSFER_BIT;
            barrier_spec_1.DestinationStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            barrier_spec_1.LayerCount            = spec.LayerCount;
            Primitives::ImageMemoryBarrier barrier_1{barrier_spec_1};

            auto command_buffer = Hardwares::VulkanDevice::BeginInstantCommandBuffer(Rendering::QueueType::GRAPHIC_QUEUE);
            command_buffer->TransitionImageLayout(barrier_0);
            command_buffer->CopyBufferToImage(staging_buffer, image_buffer, texture->m_width, texture->m_height, spec.LayerCount, barrier_0.GetHandle().newLayout);
            command_buffer->TransitionImageLayout(barrier_1);
            Hardwares::VulkanDevice::EndInstantCommandBuffer(command_buffer);
        }

        /* Cleanup resource */
        Hardwares::VulkanDevice::EnqueueBufferForDeletion(staging_buffer);
    }

    Texture2D::~Texture2D()
    {
        Dispose();
    }

} // namespace ZEngine::Rendering::Textures
