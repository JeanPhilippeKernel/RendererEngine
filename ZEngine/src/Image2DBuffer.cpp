#include <pch.h>
#include <Rendering/Buffers/Image2DBuffer.h>

namespace ZEngine::Rendering::Buffers
{
    Image2DBuffer::Image2DBuffer(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage_flag_bit, VkImageAspectFlagBits image_aspect_flag_bit)
        : m_width(width), m_height(height)
    {
        ZENGINE_VALIDATE_ASSERT(m_width > 0, "Image width must be greater then zero")
        ZENGINE_VALIDATE_ASSERT(m_height > 0, "Image height must be greater then zero")

        m_buffer_image = Hardwares::VulkanDevice::CreateImage(
            m_width,
            m_height,
            VK_IMAGE_TYPE_2D,
            format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_LAYOUT_UNDEFINED,
            usage_flag_bit,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_SAMPLE_COUNT_1_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            image_aspect_flag_bit);
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

    void Image2DBuffer::Dispose()
    {
        if (m_buffer_image)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::IMAGEVIEW, m_buffer_image.ViewHandle);
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::IMAGE, m_buffer_image.Handle);
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::BUFFERMEMORY, m_buffer_image.Memory);
            m_buffer_image = {};
        }
    }

    VkImageView Image2DBuffer::GetImageViewHandle() const
    {
        return m_buffer_image.ViewHandle;
    }
} // namespace ZEngine::Rendering::Buffers