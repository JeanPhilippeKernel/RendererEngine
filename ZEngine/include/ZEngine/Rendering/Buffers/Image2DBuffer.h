#pragma once
#include <vulkan/vulkan.h>
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Specifications/TextureSpecification.h>

namespace ZEngine::Rendering::Buffers
{
    struct Image2DBuffer : public Helpers::RefCounted
    {
        Image2DBuffer() = default;
        Image2DBuffer(const Specifications::Image2DBufferSpecification& spec);
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