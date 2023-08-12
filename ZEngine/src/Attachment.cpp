#include <Rendering/Renderers/RenderPasses/Attachment.h>
#include <Hardwares/VulkanDevice.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    Attachment::Attachment(const Specifications::AttachmentSpecification& spec)
    {
        ZENGINE_VALIDATE_ASSERT(!spec.ColorsMap.empty(), "Color attachments can't be empty")

        VkSubpassDescription                 subpass_description                   = {};
        std::vector<VkAttachmentDescription> attachment_description_collection     = {};
        VkAttachmentReference                depth_attachment_reference            = {.attachment = UINT32_MAX, .layout = VK_IMAGE_LAYOUT_UNDEFINED};
        std::vector<VkAttachmentReference>   color_attachment_reference_collection = {};
        std::vector<VkSubpassDependency>     subpass_dependency_collection         = {};

        for (int i = 0; i < spec.ColorsMap.size(); ++i)
        {
            const auto& color = spec.ColorsMap.at(i);

            // Determine the right Image format
            VkFormat color_format = VK_FORMAT_UNDEFINED;
            if (color.Format == ImageFormat::FORMAT_FROM_DEVICE)
            {
                color_format = VulkanDevice::GetSurfaceFormat().format;
            }
            else if (color.Format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
            {
                color_format = VulkanDevice::FindDepthFormat();
            }
            else
            {
                color_format = ImageFormatMap[static_cast<uint32_t>(color.Format)];
            }

            // The actual Attachment description
            VkAttachmentDescription description = {};
            description.samples                 = VK_SAMPLE_COUNT_1_BIT;
            description.format                  = color_format;
            description.loadOp                  = AttachmentLoadOperationMap[static_cast<uint32_t>(color.Load)];
            description.storeOp                 = AttachmentStoreOperationMap[static_cast<uint32_t>(color.Store)];
            description.initialLayout           = ImageLayoutMap[static_cast<uint32_t>(color.Initial)];
            description.finalLayout             = ImageLayoutMap[static_cast<uint32_t>(color.Final)];
            description.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            description.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkAttachmentReference reference     = {};
            reference.attachment                = i;
            reference.layout                    = ImageLayoutMap[static_cast<uint32_t>(color.ReferenceLayout)];

            attachment_description_collection.emplace_back(description);

            // VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL = 1000117000,
            // VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL = 1000117001,
            // VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL = 1000241000,
            // VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL = 1000241001,
            // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3,
            // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL = 4,
            if (reference.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                depth_attachment_reference = reference;
            }
            else
            {
                color_attachment_reference_collection.emplace_back(reference);
            }
        }

        for (int i = 0; i < spec.DependenciesMap.size(); ++i)
        {
            subpass_dependency_collection.push_back(spec.DependenciesMap.at(i));
        }

        subpass_description.colorAttachmentCount       = color_attachment_reference_collection.size();
        subpass_description.pColorAttachments          = color_attachment_reference_collection.data();
        subpass_description.pDepthStencilAttachment    = (depth_attachment_reference.attachment != UINT32_MAX) ? &depth_attachment_reference : nullptr;
        VkRenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.attachmentCount        = attachment_description_collection.size();
        render_pass_create_info.pAttachments           = attachment_description_collection.data();
        render_pass_create_info.subpassCount           = 1;
        render_pass_create_info.pSubpasses             = &subpass_description;
        render_pass_create_info.dependencyCount        = subpass_dependency_collection.size();
        render_pass_create_info.pDependencies          = subpass_dependency_collection.data();

        auto device = Hardwares::VulkanDevice::GetNativeDeviceHandle();
        ZENGINE_VALIDATE_ASSERT(vkCreateRenderPass(device, &render_pass_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create render pass")
    }

    Attachment::~Attachment()
    {
        Dispose();
    }

    void Attachment::Dispose()
    {
        if (m_handle)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(DeviceResourceType::RENDERPASS, m_handle);
            m_handle = VK_NULL_HANDLE;
        }
    }

    VkRenderPass Attachment::GetHandle() const
    {
        return m_handle;
    }

    Ref<Attachment> Attachment::Create(const Specifications::AttachmentSpecification& spec)
    {
        auto attachment = CreateRef<Attachment>(spec);
        return attachment;
    }
} // namespace ZEngine::Rendering::Renderers::RenderPasses