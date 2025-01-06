#pragma once
#include <Helpers/IntrusivePtr.h>
#include <Rendering/Specifications/AttachmentSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct Attachment : public Helpers::RefCounted
    {
        Attachment(Hardwares::VulkanDevice* device, const Specifications::AttachmentSpecification& spec);
        ~Attachment();
        void                                           Dispose();

        VkRenderPass                                   GetHandle() const;
        const Specifications::AttachmentSpecification& GetSpecification() const;

        uint32_t                                       GetColorAttachmentCount() const;
        uint32_t                                       GetDepthAttachmentCount() const;

    private:
        uint32_t                                m_color_attachment_count{0};
        uint32_t                                m_depth_attachment_count{0};
        Specifications::AttachmentSpecification m_specification;
        VkRenderPass                            m_handle{VK_NULL_HANDLE};
        Hardwares::VulkanDevice*                m_device{nullptr};
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses