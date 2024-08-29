#include <pch.h>
#include <Rendering/Buffers/Image2DBuffer.h>

namespace ZEngine::Rendering::Buffers
{
    Image2DBuffer::Image2DBuffer(const Specifications::Image2DBufferSpecification& spec)
        : m_width(spec.width), m_height(spec.height)
    {
        ZENGINE_VALIDATE_ASSERT(m_width > 0, "Image width must be greater then zero")
        ZENGINE_VALIDATE_ASSERT(m_height > 0, "Image height must be greater then zero")

        m_buffer_image = Hardwares::VulkanDevice::CreateImage(
            m_width,
            m_height,
            VK_IMAGE_TYPE_2D,
            Specifications::ImageViewTypeMap[VALUE_FROM_SPEC_MAP(spec.image_view_type)],
            spec.image_format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            spec.image_usage,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_SAMPLE_COUNT_1_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            spec.image_aspect_flag, spec.layer_count, spec.image_create_flag_bit);
    }

    Image2DBuffer::~Image2DBuffer()
    {
        Dispose();
    }

    Hardwares::BufferImage& Image2DBuffer::GetBuffer()
    {
        return m_buffer_image;
    }

    const Hardwares::BufferImage& Image2DBuffer::GetBuffer() const
    {
        return m_buffer_image;
    }

    VkImage Image2DBuffer::GetHandle() const
    {
        return m_buffer_image.Handle;
    }

    VkSampler Image2DBuffer::GetSampler() const
    {
        return m_buffer_image.Sampler;
    }

    void Image2DBuffer::Dispose()
    {
        if (this && m_buffer_image)
        {
            Hardwares::VulkanDevice::EnqueueBufferImageForDeletion(m_buffer_image);
            m_buffer_image = {};
        }
    }

    VkImageView Image2DBuffer::GetImageViewHandle() const
    {
        return m_buffer_image.ViewHandle;
    }
} // namespace ZEngine::Rendering::Buffers