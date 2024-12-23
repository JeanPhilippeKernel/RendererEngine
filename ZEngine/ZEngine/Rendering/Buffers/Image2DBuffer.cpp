#include <pch.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Buffers/Image2DBuffer.h>

namespace ZEngine::Rendering::Buffers
{
    Image2DBuffer::Image2DBuffer(Hardwares::VulkanDevice* device, const Specifications::Image2DBufferSpecification& spec)
        : m_device(device), m_width(spec.Width), m_height(spec.Height)
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

        m_buffer_image = m_device->CreateImage(
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

    BufferImage& Image2DBuffer::GetBuffer()
    {
        return m_buffer_image;
    }

    const BufferImage& Image2DBuffer::GetBuffer() const
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
            m_device->EnqueueBufferImageForDeletion(m_buffer_image);
            m_buffer_image = {};
        }
    }

    VkDescriptorImageInfo& Image2DBuffer::GetDescriptorImageInfo()
    {
        m_image_info.sampler     = m_buffer_image.Sampler;
        m_image_info.imageView   = m_buffer_image.ViewHandle;
        m_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return m_image_info;
    }

    VkImageView Image2DBuffer::GetImageViewHandle() const
    {
        return m_buffer_image.ViewHandle;
    }
} // namespace ZEngine::Rendering::Buffers