#pragma once
#include <GraphicBuffer.h>
#include <Rendering/Specifications/TextureSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Buffers
{
    struct Image2DBuffer : public Helpers::RefCounted
    {
        Image2DBuffer() = default;
        Image2DBuffer(Hardwares::VulkanDevice* device, const Specifications::Image2DBufferSpecification& spec);
        ~Image2DBuffer();

        BufferImage&       GetBuffer();
        const BufferImage& GetBuffer() const;
        VkImageView        GetImageViewHandle() const;
        VkImage            GetHandle() const;
        VkSampler          GetSampler() const;

        void Dispose();

    private:
        uint32_t                 m_width{1};
        uint32_t                 m_height{1};
        BufferImage              m_buffer_image;
        Hardwares::VulkanDevice* m_device{nullptr};
    };
} // namespace ZEngine::Rendering::Buffers