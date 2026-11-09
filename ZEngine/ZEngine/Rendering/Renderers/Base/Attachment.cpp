#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
#include <ZEngine/ZEngineDef.h>
#include <array>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Helpers;

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    namespace
    {
        VkFormat ResolveFormat(Hardwares::VulkanDevice* device, ImageFormat format)
        {
            if (!device || static_cast<uint32_t>(format) >= std::size(ImageFormatMap))
            {
                return VK_FORMAT_UNDEFINED;
            }
            if (format == ImageFormat::FORMAT_FROM_DEVICE)
            {
                return device->SurfaceFormat.format;
            }
            if (format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
            {
                if (!device->PhysicalDevice)
                {
                    return VK_FORMAT_UNDEFINED;
                }
                for (const auto candidate : device->DefaultDepthFormats)
                {
                    if (candidate == VK_FORMAT_UNDEFINED)
                    {
                        continue;
                    }
                    VkFormatProperties properties = {};
                    vkGetPhysicalDeviceFormatProperties(device->PhysicalDevice, candidate, &properties);
                    if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                    {
                        return candidate;
                    }
                }
                return VK_FORMAT_UNDEFINED;
            }
            return ImageFormatMap[VALUE_FROM_SPEC_MAP(format)];
        }

        bool HasStencilComponent(VkFormat format)
        {
            return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_S8_UINT;
        }

    } // namespace

    Attachment::Attachment(Hardwares::VulkanDevice* device, Specifications::AttachmentSpecification spec)
        : m_device(device), m_specification(std::move(spec)){ZENGINE_VALIDATE_ASSERT(TryPrepare(), "Failed to prepare render pass")}

          Attachment::Attachment(Hardwares::VulkanDevice * device, Specifications::AttachmentSpecification spec, VkResult * status)
        : m_device(device), m_specification(std::move(spec))
    {
        (void) TryPrepare(status);
    }

    bool Attachment::TryPrepare(VkResult* status, PFN_vkCreateRenderPass create_render_pass)
    {
        const auto finish = [&](VkResult result) {
            if (status)
            {
                *status = result;
            }
            if (m_device)
            {
                m_device->CheckDeviceLost(result, "Render pass preparation");
            }
            return result == VK_SUCCESS;
        };
        if (!m_device)
        {
            return finish(VK_ERROR_INITIALIZATION_FAILED);
        }
        if (m_device->IsDeviceLost.load(std::memory_order_acquire))
        {
            return finish(VK_ERROR_DEVICE_LOST);
        }
        if (m_handle)
        {
            return finish(VK_SUCCESS);
        }
        const auto count            = m_specification.ColorsMap.size();
        const auto dependency_count = m_specification.DependenciesMap.size();
        if (!count || count > MaxAttachments || dependency_count > MaxDependencies || m_specification.BindPoint != PipelineBindPoint::GRAPHIC)
        {
            return finish(VK_ERROR_INITIALIZATION_FAILED);
        }
        std::array<VkAttachmentDescription, MaxAttachments>   attachments  = {};
        std::array<VkAttachmentReference, MaxAttachments - 1> colors       = {};
        std::array<VkSubpassDependency, MaxDependencies>      dependencies = {};
        VkAttachmentReference                                 depth        = {.attachment = VK_ATTACHMENT_UNUSED, .layout = VK_IMAGE_LAYOUT_UNDEFINED};
        uint32_t                                              color_count  = 0;
        uint32_t                                              depth_count  = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto* entry = m_specification.ColorsMap.find(i);
            if (!entry || static_cast<uint32_t>(entry->Load) >= std::size(AttachmentLoadOperationMap) || static_cast<uint32_t>(entry->Store) >= std::size(AttachmentStoreOperationMap) || static_cast<uint32_t>(entry->Initial) >= std::size(ImageLayoutMap) || static_cast<uint32_t>(entry->Final) >= std::size(ImageLayoutMap) || static_cast<uint32_t>(entry->ReferenceLayout) >= std::size(ImageLayoutMap))
            {
                return finish(VK_ERROR_INITIALIZATION_FAILED);
            }
            const auto& color        = *entry;
            const auto  color_format = ResolveFormat(m_device, color.Format);
            if (color_format == VK_FORMAT_UNDEFINED)
            {
                return finish(VK_ERROR_FORMAT_NOT_SUPPORTED);
            }
            auto& description               = attachments[i];
            description.samples             = VK_SAMPLE_COUNT_1_BIT;
            description.format              = color_format;
            description.loadOp              = AttachmentLoadOperationMap[static_cast<uint32_t>(color.Load)];
            description.storeOp             = AttachmentStoreOperationMap[static_cast<uint32_t>(color.Store)];
            description.initialLayout       = ImageLayoutMap[static_cast<uint32_t>(color.Initial)];
            description.finalLayout         = ImageLayoutMap[static_cast<uint32_t>(color.Final)];
            description.stencilLoadOp       = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            description.stencilStoreOp      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkAttachmentReference reference = {};
            reference.attachment            = i;
            reference.layout                = ImageLayoutMap[static_cast<uint32_t>(color.ReferenceLayout)];

            if (IsDepthOrStencilFormat(color_format))
            {
                if (++depth_count > 1)
                {
                    return finish(VK_ERROR_INITIALIZATION_FAILED);
                }
                depth = reference;
            }
            else
            {
                if (color_count == colors.size())
                {
                    return finish(VK_ERROR_INITIALIZATION_FAILED);
                }
                colors[color_count++] = reference;
            }
        }
        for (uint32_t i = 0; i < dependency_count; ++i)
        {
            const auto* dependency = m_specification.DependenciesMap.find(i);
            if (!dependency || (dependency->srcSubpass != 0 && dependency->srcSubpass != VK_SUBPASS_EXTERNAL) || (dependency->dstSubpass != 0 && dependency->dstSubpass != VK_SUBPASS_EXTERNAL) || (dependency->srcSubpass == VK_SUBPASS_EXTERNAL && dependency->dstSubpass == VK_SUBPASS_EXTERNAL))
            {
                return finish(VK_ERROR_INITIALIZATION_FAILED);
            }
            dependencies[i] = *dependency;
        }
        if (!m_device->PhysicalDeviceSupportDynamicRendering)
        {
            if (!m_device->LogicalDevice || !create_render_pass)
            {
                return finish(VK_ERROR_INITIALIZATION_FAILED);
            }
        }
        else
        {
            m_color_attachment_count = color_count;
            m_depth_attachment_count = depth_count;
            return finish(VK_SUCCESS); // No unused legacy object for dynamic rendering.
        }
        VkSubpassDescription subpass_description       = {};
        subpass_description.pipelineBindPoint          = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass_description.colorAttachmentCount       = color_count;
        subpass_description.pColorAttachments          = colors.data();
        subpass_description.pDepthStencilAttachment    = depth_count ? &depth : nullptr;
        VkRenderPassCreateInfo render_pass_create_info = {};
        render_pass_create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_create_info.attachmentCount        = static_cast<uint32_t>(count);
        render_pass_create_info.pAttachments           = attachments.data();
        render_pass_create_info.subpassCount           = 1;
        render_pass_create_info.pSubpasses             = &subpass_description;
        render_pass_create_info.dependencyCount        = static_cast<uint32_t>(dependency_count);
        render_pass_create_info.pDependencies          = dependencies.data();
        VkRenderPass incoming                          = VK_NULL_HANDLE;
        const auto   result                            = create_render_pass(m_device->LogicalDevice, &render_pass_create_info, nullptr, &incoming);
        if (result == VK_SUCCESS)
        {
            m_handle                 = incoming;
            m_color_attachment_count = color_count;
            m_depth_attachment_count = depth_count;
        }
        return finish(result);
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

    bool Attachment::IsRenderPassCompatibleWith(const Attachment& other) const
    {
        if (m_device != other.m_device || m_specification.BindPoint != other.m_specification.BindPoint || m_specification.ColorsMap.size() != other.m_specification.ColorsMap.size() || m_specification.DependenciesMap.size() != other.m_specification.DependenciesMap.size())
        {
            return false;
        }
        // Construction fixes one graphics subpass, sample count 1, ordered
        // colors, at most one depth attachment, and no input/resolve/preserve
        // attachments or multiview. Layouts and load/store operations do not
        // affect Vulkan render-pass compatibility. Dependencies do.
        for (uint32_t index = 0; index < m_specification.ColorsMap.size(); ++index)
        {
            const auto* left  = m_specification.ColorsMap.find(index);
            const auto* right = other.m_specification.ColorsMap.find(index);
            if (!left || !right || ResolveFormat(m_device, left->Format) != ResolveFormat(m_device, right->Format))
            {
                return false;
            }
        }
        for (uint32_t index = 0; index < m_specification.DependenciesMap.size(); ++index)
        {
            const auto* left  = m_specification.DependenciesMap.find(index);
            const auto* right = other.m_specification.DependenciesMap.find(index);
            if (!left || !right || left->srcSubpass != right->srcSubpass || left->dstSubpass != right->dstSubpass || left->srcStageMask != right->srcStageMask || left->dstStageMask != right->dstStageMask || left->srcAccessMask != right->srcAccessMask || left->dstAccessMask != right->dstAccessMask || left->dependencyFlags != right->dependencyFlags)
            {
                return false;
            }
        }
        return true;
    }

    uint32_t Attachment::GetColorAttachmentCount() const
    {
        return m_color_attachment_count;
    }

    VkFormat Attachment::GetResolvedFormat(uint32_t index) const
    {
        const auto* attachment = m_specification.ColorsMap.find(index);
        return attachment ? ResolveFormat(m_device, attachment->Format) : VK_FORMAT_UNDEFINED;
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
