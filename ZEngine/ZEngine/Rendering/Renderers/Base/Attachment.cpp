#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
#include <ZEngine/ZEngineDef.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    namespace
    {
        VkFormat ResolveFormat(Hardwares::VulkanDevice* device, ImageFormat format)
        {
            if (format == ImageFormat::FORMAT_FROM_DEVICE)
                return device->SurfaceFormat.format;
            if (format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
                return device->FindDepthFormat();
            return ImageFormatMap[VALUE_FROM_SPEC_MAP(format)];
        }

        bool HasStencilComponent(VkFormat format)
        {
            return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_S8_UINT;
        }

        bool IsDepthOrStencilFormat(VkFormat format)
        {
            return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_S8_UINT;
        }
    } // namespace

    Attachment::Attachment(Hardwares::VulkanDevice* device, Specifications::AttachmentSpecification spec) : m_device(device), m_specification(std::move(spec))
    {
        ZENGINE_VALIDATE_ASSERT(!m_specification.ColorsMap.empty(), "Color attachments can't be empty")

        for (uint32_t index = 0; index < m_specification.ColorsMap.size(); ++index)
        {
            const VkFormat format = ResolveFormat(m_device, m_specification.ColorsMap.at(index).Format);
            if (IsDepthOrStencilFormat(format))
                ++m_depth_attachment_count;
            else
                ++m_color_attachment_count;
        }

        // Dynamic rendering consumes this specification directly. Retaining a
        // VkRenderPass here would create an unused legacy execution object.
        if (m_device->PhysicalDeviceSupportDynamicRendering)
            return;

        auto                           scratch                               = ZGetScratch(device->Arena);

        VkSubpassDescription           subpass_description                   = {};
        VkAttachmentReference          depth_attachment_reference            = {.attachment = VK_ATTACHMENT_UNUSED, .layout = VK_IMAGE_LAYOUT_UNDEFINED};

        Array<VkAttachmentDescription> attachment_description_collection     = {};
        Array<VkAttachmentReference>   color_attachment_reference_collection = {};
        Array<VkSubpassDependency>     subpass_dependency_collection         = {};

        attachment_description_collection.init(scratch.Arena, 5);
        color_attachment_reference_collection.init(scratch.Arena, 5);
        subpass_dependency_collection.init(scratch.Arena, 5);

        for (uint32_t i = 0; i < m_specification.ColorsMap.size(); ++i)
        {
            const auto&             color        = m_specification.ColorsMap.at(i);

            // Determine the right Image format
            VkFormat                color_format = ResolveFormat(m_device, color.Format);

            // The actual Attachment description
            VkAttachmentDescription description  = {};
            description.samples                  = VK_SAMPLE_COUNT_1_BIT;
            description.format                   = color_format;
            description.loadOp                   = AttachmentLoadOperationMap[static_cast<uint32_t>(color.Load)];
            description.storeOp                  = AttachmentStoreOperationMap[static_cast<uint32_t>(color.Store)];
            description.initialLayout            = ImageLayoutMap[static_cast<uint32_t>(color.Initial)];
            description.finalLayout              = ImageLayoutMap[static_cast<uint32_t>(color.Final)];
            description.stencilLoadOp            = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            description.stencilStoreOp           = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkAttachmentReference reference      = {};
            reference.attachment                 = i;
            reference.layout                     = ImageLayoutMap[static_cast<uint32_t>(color.ReferenceLayout)];

            attachment_description_collection.push(description);

            // VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL = 1000117000,
            // VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL = 1000117001,
            // VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL = 1000241000,
            // VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL = 1000241001,
            // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL = 3,
            // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL = 4,
            if (IsDepthOrStencilFormat(color_format))
            {
                depth_attachment_reference = reference;
            }
            else
            {
                color_attachment_reference_collection.push(reference);
            }
        }

        for (int i = 0; i < spec.DependenciesMap.size(); ++i)
        {
            subpass_dependency_collection.push(m_specification.DependenciesMap.at(i));
        }

        subpass_description.colorAttachmentCount       = color_attachment_reference_collection.size();
        subpass_description.pColorAttachments          = color_attachment_reference_collection.data();
        subpass_description.pDepthStencilAttachment    = (depth_attachment_reference.attachment != VK_ATTACHMENT_UNUSED) ? &depth_attachment_reference : nullptr;
        VkRenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.attachmentCount        = attachment_description_collection.size();
        render_pass_create_info.pAttachments           = attachment_description_collection.data();
        render_pass_create_info.subpassCount           = 1;
        render_pass_create_info.pSubpasses             = &subpass_description;
        render_pass_create_info.dependencyCount        = subpass_dependency_collection.size();
        render_pass_create_info.pDependencies          = subpass_dependency_collection.data();

        ZENGINE_VALIDATE_ASSERT(vkCreateRenderPass(m_device->LogicalDevice, &render_pass_create_info, nullptr, &m_handle) == VK_SUCCESS, "Failed to create render pass")

        ZReleaseScratch(scratch);
    }

    Attachment::~Attachment()
    {
        Dispose();
    }

    void Attachment::Dispose()
    {
        if (m_handle)
        {
            Hardwares::DeferredFreeEntry e = {};
            e.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
            e.Data.Vk                      = {m_handle, DeviceResourceType::RENDERPASS, nullptr};
            m_device->DeferFree(e);
            m_handle = VK_NULL_HANDLE;
        }
    }

    VkRenderPass Attachment::GetHandle() const
    {
        return m_handle;
    }

    const Specifications::AttachmentSpecification& Attachment::GetSpecification() const
    {
        return m_specification;
    }

    uint32_t Attachment::GetColorAttachmentCount() const
    {
        return m_color_attachment_count;
    }

    uint32_t Attachment::GetDepthAttachmentCount() const
    {
        return m_depth_attachment_count;
    }

    uint32_t Attachment::GetDynamicRenderingFormats(VkFormat* color_formats, uint32_t color_format_capacity, VkFormat* depth_format, VkFormat* stencil_format) const
    {
        ZENGINE_VALIDATE_ASSERT(color_formats != nullptr || m_color_attachment_count == 0, "Dynamic-rendering color format output is null")
        ZENGINE_VALIDATE_ASSERT(depth_format != nullptr && stencil_format != nullptr, "Dynamic-rendering depth/stencil format output is null")
        ZENGINE_VALIDATE_ASSERT(color_format_capacity >= m_color_attachment_count, "Dynamic-rendering color format capacity exceeded")

        *depth_format        = VK_FORMAT_UNDEFINED;
        *stencil_format      = VK_FORMAT_UNDEFINED;
        uint32_t color_index = 0;
        for (uint32_t index = 0; index < m_specification.ColorsMap.size(); ++index)
        {
            const ColorAttachment& attachment = m_specification.ColorsMap.at(index);
            const VkFormat         format     = ResolveFormat(m_device, attachment.Format);
            if (IsDepthOrStencilFormat(format))
            {
                *depth_format = format;
                if (HasStencilComponent(format))
                    *stencil_format = format;
            }
            else
            {
                color_formats[color_index++] = format;
            }
        }
        return color_index;
    }
} // namespace ZEngine::Rendering::Renderers::RenderPasses
