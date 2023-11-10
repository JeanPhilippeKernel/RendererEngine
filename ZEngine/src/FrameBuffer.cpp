#include <pch.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Logging/LoggerDefinition.h>

using namespace  ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Buffers
{
    FramebufferVNext::FramebufferVNext(const Specifications::FrameBufferSpecificationVNext& specification) : m_framebuffer_specification(specification)
    {
        Create();
    }

    FramebufferVNext::~FramebufferVNext()
    {
        Dispose();
    }

    VkRenderPass FramebufferVNext::GetRenderPass() const
    {
        if (m_attachment)
        {
            return m_attachment->GetHandle();
        }
        return VK_NULL_HANDLE;
    }

    VkFramebuffer FramebufferVNext::GetHandle() const
    {
        return m_handle;
    }

    VkSampler FramebufferVNext::GetSampler() const
    {
        return m_sampler;
    }

    uint32_t FramebufferVNext::GetWidth() const
    {
        return m_width;
    }

    uint32_t FramebufferVNext::GetHeight() const
    {
        return m_height;
    }

    FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification()
    {
        return m_framebuffer_specification;
    }

    const FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification() const
    {
        return m_framebuffer_specification;
    }

    void FramebufferVNext::Create()
    {
        this->m_width  = m_framebuffer_specification.Width;
        this->m_height = m_framebuffer_specification.Height;
        this->m_layers = m_framebuffer_specification.Layers;

        uint32_t                                image_format_index       = 0;
        Specifications::AttachmentSpecification attachment_specification = {};
        attachment_specification.BindPoint                               = PipelineBindPoint::GRAPHIC;

        for (auto image_format : m_framebuffer_specification.AttachmentSpecifications.FormatCollection)
        {
            if (image_format == ImageFormat::R8G8B8A8_UNORM)
            {
                auto color_attachment = CreateRef<Image2DBuffer>(
                    m_width,
                    m_height,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT);

                m_color_attachment_collection.push_back(color_attachment);

                attachment_specification.ColorsMap[image_format_index]                 = {};
                attachment_specification.ColorsMap[image_format_index].Format          = ImageFormat::R8G8B8A8_UNORM;
                attachment_specification.ColorsMap[image_format_index].Load            = LoadOperation::CLEAR;
                attachment_specification.ColorsMap[image_format_index].Store           = StoreOperation::STORE;
                attachment_specification.ColorsMap[image_format_index].Initial         = ImageLayout::UNDEFINED;
                attachment_specification.ColorsMap[image_format_index].Final           = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                attachment_specification.ColorsMap[image_format_index].ReferenceLayout = ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
            }
            else if (image_format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
            {
                attachment_specification.ColorsMap[image_format_index]                 = {};
                attachment_specification.ColorsMap[image_format_index].Format          = ImageFormat::DEPTH_STENCIL_FROM_DEVICE;
                attachment_specification.ColorsMap[image_format_index].Load            = LoadOperation::CLEAR;
                attachment_specification.ColorsMap[image_format_index].Store           = StoreOperation::DONT_CARE;
                attachment_specification.ColorsMap[image_format_index].Initial         = ImageLayout::UNDEFINED;
                attachment_specification.ColorsMap[image_format_index].Final           = ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                attachment_specification.ColorsMap[image_format_index].ReferenceLayout = ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                auto depth_format  = Hardwares::VulkanDevice::FindDepthFormat();
                m_depth_attachment = CreateRef<Image2DBuffer>(m_width, m_height, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
            }
            image_format_index++;
        }

        /*Subpass definition*/
        attachment_specification.DependenciesMap[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
        attachment_specification.DependenciesMap[0].dstSubpass      = 0;
        attachment_specification.DependenciesMap[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        attachment_specification.DependenciesMap[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        attachment_specification.DependenciesMap[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        attachment_specification.DependenciesMap[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        attachment_specification.DependenciesMap[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        attachment_specification.DependenciesMap[1].srcSubpass      = 0;
        attachment_specification.DependenciesMap[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
        attachment_specification.DependenciesMap[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        attachment_specification.DependenciesMap[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        attachment_specification.DependenciesMap[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        attachment_specification.DependenciesMap[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
        attachment_specification.DependenciesMap[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        m_attachment                                                = Renderers::RenderPasses::Attachment::Create(attachment_specification);

        /* Create the actual Framebuffer */
        std::vector<VkImageView> attachment_view_collection = {};
        for (const Ref<Image2DBuffer>& buffer : m_color_attachment_collection)
        {
            attachment_view_collection.push_back(buffer->GetImageViewHandle());
        }
        attachment_view_collection.push_back(m_depth_attachment->GetImageViewHandle());

        this->m_handle = Hardwares::VulkanDevice::CreateFramebuffer(
            attachment_view_collection, m_attachment->GetHandle(), m_framebuffer_specification.Width, m_framebuffer_specification.Height, m_framebuffer_specification.Layers);

        /* Create Sampler */
        this->m_sampler = Hardwares::VulkanDevice::CreateImageSampler();
    }

    void FramebufferVNext::Invalidate()
    {
        this->Dispose();
    }

    void FramebufferVNext::Dispose()
    {
        for (Ref<Image2DBuffer>& buffer : m_color_attachment_collection)
        {
            buffer->Dispose();
        }
        m_color_attachment_collection.clear();
        m_color_attachment_collection.shrink_to_fit();

        m_depth_attachment->Dispose();

        if (m_sampler)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::SAMPLER, m_sampler);
            m_sampler = VK_NULL_HANDLE;
        }

        if (m_handle)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::FRAMEBUFFER, m_handle);
            m_handle = VK_NULL_HANDLE;
        }

        m_attachment->Dispose();
    }

    const std::vector<Ref<Image2DBuffer>>& FramebufferVNext::GetColorAttachmentCollection() const
    {
        return m_color_attachment_collection;
    }

    Ref<Image2DBuffer> FramebufferVNext::GetDepthAttachment() const
    {
        return m_depth_attachment;
    }
} // namespace ZEngine::Rendering::Buffers
