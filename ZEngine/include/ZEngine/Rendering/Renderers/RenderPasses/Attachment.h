#pragma once
#include <ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <Rendering/Specifications/AttachmentSpecification.h>

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct Attachment : public Helpers::RefCounted
    {
        Attachment(const Specifications::AttachmentSpecification& spec);
        ~Attachment();
        void Dispose();

        VkRenderPass                                   GetHandle() const;
        const Specifications::AttachmentSpecification& GetSpecification() const;

        uint32_t GetColorAttachmentCount() const;
        uint32_t GetDepthAttachmentCount() const;


        static Ref<Attachment> Create(const Specifications::AttachmentSpecification&);

    private:
        uint32_t                                m_color_attachment_count{0};
        uint32_t                                m_depth_attachment_count{0};
        Specifications::AttachmentSpecification m_specification;
        VkRenderPass                            m_handle{VK_NULL_HANDLE};
    };
}