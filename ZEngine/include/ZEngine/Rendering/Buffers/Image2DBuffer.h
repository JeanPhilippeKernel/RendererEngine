#pragma once
#include <vulkan/vulkan.h>
#include <Hardwares/VulkanDevice.h>

namespace ZEngine::Rendering::Buffers
{
    struct Image2DBuffer : public Helpers::RefCounted
    {
        Image2DBuffer() = default;
        Image2DBuffer(
            uint32_t              width,
            uint32_t              height,
            VkFormat              format,
            VkImageUsageFlags     usage_flag_bit,
            VkImageAspectFlagBits image_aspect_flag_bit,
            uint32_t              layer_count           = 1U,
            VkImageCreateFlags    image_create_flag_bit = 0);
        ~Image2DBuffer();

        Hardwares::BufferImage&       GetBuffer();
        const Hardwares::BufferImage& GetBuffer() const;
        VkImageView                   GetImageViewHandle() const;
        VkImage                       GetHandle() const;
        VkSampler                     GetSampler() const;

        void Dispose();

    private:
        uint32_t               m_width{1};
        uint32_t               m_height{1};
        Hardwares::BufferImage m_buffer_image;
    };
}