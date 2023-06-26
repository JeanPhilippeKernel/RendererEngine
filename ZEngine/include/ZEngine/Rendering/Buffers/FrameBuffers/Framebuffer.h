#pragma once
#include <vector>
#include <array>
#include <vulkan/vulkan.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>
//#include <Core/IGraphicObject.h>
//#include <Rendering/Buffers/PixelBuffer.h>

namespace ZEngine::Rendering::Buffers
{
    struct FramebufferVNext
    {
        FramebufferVNext() = default;
        FramebufferVNext(const FrameBufferSpecificationVNext&);
        uint32_t                   Width  = 1;
        uint32_t                   Height = 1;
        uint32_t                   Layers = 1;
        VkRenderPass               RenderPass{VK_NULL_HANDLE};
        VkFramebuffer              Framebuffer{VK_NULL_HANDLE};
        VkSampler                  Sampler{VK_NULL_HANDLE};
        std::array<VkImageView, 2> AttachmentCollection;

        void Dispose();

    private:
        VkImage         m_color_image_attachment{VK_NULL_HANDLE};
        VkDeviceMemory  m_color_image_memory{VK_NULL_HANDLE};
        VkImage         m_depth_image_attachment{VK_NULL_HANDLE};
        VkDeviceMemory  m_depth_image_memory{VK_NULL_HANDLE};
    };

    //class FrameBuffer : public Core::IGraphicObject
    //{
    //public:
    //    FrameBuffer(const FrameBufferSpecification&);
    //    virtual ~FrameBuffer();

    //    virtual GLuint GetIdentifier() const override;

    //    void Resize(uint32_t width, uint32_t height);

    //    void Bind();
    //    void Unbind();

    //    void ClearColorAttachments();

    //    const FrameBufferSpecification& GetSpecification() const;
    //    FrameBufferSpecification&       GetSpecification();

    //    uint32_t GetTexture(uint32_t color_attachment_index = 0) const;

    //    int ReadPixelAt(int32_t pixel_pos_x, int32_t pixel_pos_y, uint32_t color_attachment_index = 0);

    //private:
    //    bool                     m_is_binding{false};
    //    GLuint                   m_framebuffer_identifier;
    //    GLuint                   m_texture_depth_attachment;
    //    std::vector<GLuint>      m_texture_color_attachments;
    //    FrameBufferSpecification m_specification;
    //    PixelBuffer<int>         m_pixel_buffer;
    //};
} // namespace ZEngine::Rendering::Buffers
