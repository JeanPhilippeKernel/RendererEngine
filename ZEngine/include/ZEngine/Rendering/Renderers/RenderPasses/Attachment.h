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

        VkRenderPass GetHandle() const;

        static Ref<Attachment> Create(const Specifications::AttachmentSpecification&);

    private:
        Specifications::AttachmentSpecification m_specification;
        VkRenderPass                            m_handle{VK_NULL_HANDLE};
    };
}