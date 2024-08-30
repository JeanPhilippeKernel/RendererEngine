#include <pch.h>
#include <Rendering/Buffers/Image2DBuffer.h>

namespace ZEngine::Rendering::Buffers
{
    Image2DBuffer::Image2DBuffer(const Specifications::Image2DBufferSpecification& spec) : m_width(spec.Width), m_height(spec.Height)
    {
        ZENGINE_VALIDATE_ASSERT(m_width > 0, "Image width must be greater then zero")
        ZENGINE_VALIDATE_ASSERT(m_height > 0, "Image height must be greater then zero")

        Specifications::ImageViewType   image_view_type   = Specifications::ImageViewType::TYPE_2D;
        Specifications::ImageCreateFlag image_create_flag = Specifications::ImageCreateFlag::NONE;

        if (spec.BufferUsageType == Specifications::ImageBufferUsageType::CUBEMAP)
        {

            image_view_type   = Specifications::ImageViewType::TYPE_CUBE;
            image_create_flag = Specifications::ImageCreateFlag::CUBE_COMPATIBLE_BIT;
        }

        m_buffer_image = Hardwares::VulkanDevice::CreateImage(
            m_width,
            m_height,
            VK_IMAGE_TYPE_2D,
            Specifications::ImageViewTypeMap[VALUE_FROM_SPEC_MAP(image_view_type)],
            spec.ImageFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            spec.ImageUsage,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_SAMPLE_COUNT_1_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            spec.ImageAspectFlag,
            spec.LayerCount,
            Specifications::ImageCreateFlagMap[VALUE_FROM_SPEC_MAP(image_create_flag)]);
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