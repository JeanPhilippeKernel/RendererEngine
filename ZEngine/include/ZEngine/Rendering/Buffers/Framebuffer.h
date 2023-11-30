#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Rendering/Specifications/FrameBufferSpecification.h>
#include <Rendering/Specifications/AttachmentSpecification.h>
#include <Rendering/Textures/Texture2D.h>

namespace ZEngine::Rendering::Buffers
{
    struct FramebufferVNext : public Helpers::RefCounted
    {
        FramebufferVNext() = default;
        FramebufferVNext(const Specifications::FrameBufferSpecificationVNext&);
        FramebufferVNext(Specifications::FrameBufferSpecificationVNext&&);
        ~FramebufferVNext();

        void Create();
        void Resize(uint32_t width = 1, uint32_t height = 1);
        void Dispose();

        static Ref<FramebufferVNext> Create(const Specifications::FrameBufferSpecificationVNext&);
        static Ref<FramebufferVNext> Create(Specifications::FrameBufferSpecificationVNext&&);

        VkRenderPass  GetRenderPass() const;
        VkFramebuffer GetHandle() const;
        VkSampler     GetSampler() const;
        uint32_t      GetWidth() const;
        uint32_t      GetHeight() const;

        Specifications::FrameBufferSpecificationVNext&       GetSpecification();
        const Specifications::FrameBufferSpecificationVNext& GetSpecification() const;
        const std::vector<Ref<Textures::Texture2D>>&         GetColorAttachmentCollection() const;
        Ref<Textures::Texture2D>                             GetDepthAttachment() const;

    private:
        std::vector<Ref<Textures::Texture2D>>         m_color_attachment_collection;
        Ref<Textures::Texture2D>                      m_depth_attachment{};
        Specifications::AttachmentSpecification       m_attachment_specification{};
        Specifications::FrameBufferSpecificationVNext m_specification{};
        VkSampler                                     m_sampler{VK_NULL_HANDLE};
        VkFramebuffer                                 m_handle{VK_NULL_HANDLE};
        Ref<Renderers::RenderPasses::Attachment>      m_attachment;
    };
} // namespace ZEngine::Rendering::Buffers
