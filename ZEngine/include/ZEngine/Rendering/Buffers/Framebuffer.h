#pragma once
#include <vector>
#include <array>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Rendering/Specifications/FrameBufferSpecification.h>
#include <Rendering/Specifications/AttachmentSpecification.h>
#include <Rendering/Buffers/Image2DBuffer.h>

namespace ZEngine::Rendering::Buffers
{
    struct FramebufferVNext
    {
        FramebufferVNext() = default;
        FramebufferVNext(const Specifications::FrameBufferSpecificationVNext&);
        ~FramebufferVNext();

        VkRenderPass  GetRenderPass() const;
        VkFramebuffer GetHandle() const;
        VkSampler     GetSample() const;
        uint32_t      GetWidth() const;
        uint32_t      GetHeight() const;

        Specifications::FrameBufferSpecificationVNext&       GetSpecification();
        const Specifications::FrameBufferSpecificationVNext& GetSpecification() const;
        void                                                 Dispose();

    private:
        uint32_t                                      m_width{1};
        uint32_t                                      m_height{1};
        uint32_t                                      m_layers{1};
        std::vector<Ref<Image2DBuffer>>               m_color_attachment_collection;
        Ref<Image2DBuffer>                            m_depth_attachment;
        Specifications::AttachmentSpecification       m_attachment_specification{};
        Specifications::FrameBufferSpecificationVNext m_framebuffer_specification{};
        VkSampler                                     m_sampler{VK_NULL_HANDLE};
        VkFramebuffer                                 m_handle{VK_NULL_HANDLE};
        Ref<Renderers::RenderPasses::Attachment>      m_attachment;
    };
} // namespace ZEngine::Rendering::Buffers
