#pragma once
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Rendering/Specifications/AttachmentSpecification.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct Attachment
    {
        Attachment(Hardwares::VulkanDevice* device, Specifications::AttachmentSpecification spec);
        /// Checked creation retains the recipe on failure for a later retry.
        Attachment(Hardwares::VulkanDevice* device, Specifications::AttachmentSpecification spec, VkResult* status);
        ~Attachment();
        void                                           Dispose();
        /// Idempotent; failure never replaces an existing native object. The
        /// optional dispatch supports alternate loaders and deterministic faults.
        [[nodiscard]] bool                             TryPrepare(VkResult* status = nullptr, PFN_vkCreateRenderPass create_render_pass = vkCreateRenderPass);

        static constexpr uint32_t                      MaxAttachments  = 9; // Eight colors and one depth/stencil.
        static constexpr uint32_t                      MaxDependencies = 32;

        VkRenderPass                                   GetHandle() const;
        const Specifications::AttachmentSpecification& GetSpecification() const;
        /// Returns undefined for a missing attachment or unsupported device format.
        VkFormat                                       GetResolvedFormat(uint32_t index) const;
        /// Compatibility of the canonical single-subpass render passes this
        /// type constructs, not a generic format-only compatibility test.
        bool                                           IsRenderPassCompatibleWith(const Attachment& other) const;

        uint32_t                                       GetColorAttachmentCount() const;
        uint32_t                                       GetDepthAttachmentCount() const;

        /// @brief Writes dynamic-rendering attachment formats and returns the color-format count.
        uint32_t                                       GetDynamicRenderingFormats(VkFormat* color_formats, uint32_t color_format_capacity, VkFormat* depth_format, VkFormat* stencil_format) const;

    private:
        uint32_t                                m_color_attachment_count{0};
        uint32_t                                m_depth_attachment_count{0};
        Specifications::AttachmentSpecification m_specification;
        VkRenderPass                            m_handle{VK_NULL_HANDLE};
        Hardwares::VulkanDevice*                m_device{nullptr};
    };
} // namespace ZEngine::Rendering::Renderers::RenderPasses
