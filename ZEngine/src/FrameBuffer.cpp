#include <pch.h>
#include <Rendering/Buffers/Framebuffer.h>
#include <Logging/LoggerDefinition.h>

using namespace  ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Buffers
{
    FramebufferVNext::FramebufferVNext(const Specifications::FrameBufferSpecificationVNext& specification) : m_specification(specification)
    {
        Create();
    }

    FramebufferVNext::FramebufferVNext(Specifications::FrameBufferSpecificationVNext&& specification) : m_specification(std::move(specification))
    {
        Create();
    }

    FramebufferVNext::~FramebufferVNext()
    {
        Dispose();
    }

    Ref<FramebufferVNext> FramebufferVNext::Create(const Specifications::FrameBufferSpecificationVNext& spec)
    {
        return CreateRef<FramebufferVNext>(spec);
    }

    Ref<FramebufferVNext> FramebufferVNext::Create(Specifications::FrameBufferSpecificationVNext&& spec)
    {
        return CreateRef<FramebufferVNext>(std::move(spec));
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
        return m_specification.Width;
    }

    uint32_t FramebufferVNext::GetHeight() const
    {
        return m_specification.Height;
    }

    FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification()
    {
        return m_specification;
    }

    const FrameBufferSpecificationVNext& FramebufferVNext::GetSpecification() const
    {
        return m_specification;
    }

    void FramebufferVNext::Create()
    {
        uint32_t                                image_format_index       = 0;
        Specifications::AttachmentSpecification attachment_specification = {};
        attachment_specification.BindPoint                               = PipelineBindPoint::GRAPHIC;

        for (auto image_format : m_specification.AttachmentSpecifications.FormatCollection)
        {
            if (image_format == ImageFormat::R8G8B8A8_UNORM)
            {
                attachment_specification.ColorsMap[image_format_index]         = {};
                attachment_specification.ColorsMap[image_format_index].Format  = ImageFormat::R8G8B8A8_UNORM;
                attachment_specification.ColorsMap[image_format_index].Load    = m_specification.ClearColor ? LoadOperation::CLEAR : LoadOperation::LOAD;
                attachment_specification.ColorsMap[image_format_index].Store   = StoreOperation::STORE;
                attachment_specification.ColorsMap[image_format_index].Initial = m_specification.ClearColor ? ImageLayout::UNDEFINED : ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                attachment_specification.ColorsMap[image_format_index].Final   = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
                attachment_specification.ColorsMap[image_format_index].ReferenceLayout = ImageLayout::COLOR_ATTACHMENT_OPTIMAL;
            }
            else if (image_format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
            {
                attachment_specification.ColorsMap[image_format_index]        = {};
                attachment_specification.ColorsMap[image_format_index].Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE;
                attachment_specification.ColorsMap[image_format_index].Load   = m_specification.ClearDepth ? LoadOperation::CLEAR : LoadOperation::LOAD;
                attachment_specification.ColorsMap[image_format_index].Store  = StoreOperation::STORE;
                attachment_specification.ColorsMap[image_format_index].Initial =
                    m_specification.ClearDepth ? ImageLayout::UNDEFINED : ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                attachment_specification.ColorsMap[image_format_index].Final           = ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                attachment_specification.ColorsMap[image_format_index].ReferenceLayout = ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
            image_format_index++;
        }

        if (m_specification.InputColorAttachment.empty())
        {
            for (size_t i = 0; i < m_specification.AttachmentSpecifications.FormatCollection.size(); ++i)
            {
                // Since Color/Depth attachments can be used as Input Attachment for other RenderPasses,
                // We reuse already allocated memory instead of emplacing back new ones
                auto image_format = m_specification.AttachmentSpecifications.FormatCollection[i];
                if (image_format == ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
                {
                    Specifications::TextureSpecification spec = {};
                    spec.Width                                = m_specification.Width;
                    spec.Height                               = m_specification.Height;
                    spec.Format                               = image_format;
                    spec.PerformTransition                    = false;
                    auto depth_attachment                     = Textures::Texture2D::Create(spec);

                    if (!m_depth_attachment)
                    {
                        m_depth_attachment = std::move(depth_attachment);
                    }
                    else
                    {
                        m_depth_attachment.swapValue(depth_attachment);
                    }
                }
                else
                {
                    Specifications::TextureSpecification spec = {};
                    spec.Width                                = m_specification.Width;
                    spec.Height                               = m_specification.Height;
                    spec.Format                               = image_format;
                    spec.PerformTransition                    = false;
                    auto color_attachment                     = Textures::Texture2D::Create(spec);

                    if (i < m_color_attachment_collection.size())
                    {
                        m_color_attachment_collection[i].swapValue(color_attachment);
                    }
                    else
                    {
                        m_color_attachment_collection.push_back(std::move(color_attachment));
                    }
                }
            }
        }
        if (m_specification.InputColorAttachment.empty())
        {
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
        }
        else
        {
            /*Subpass definition*/
            attachment_specification.DependenciesMap[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
            attachment_specification.DependenciesMap[0].dstSubpass      = 0;
            attachment_specification.DependenciesMap[0].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            attachment_specification.DependenciesMap[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            attachment_specification.DependenciesMap[0].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            attachment_specification.DependenciesMap[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            attachment_specification.DependenciesMap[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            attachment_specification.DependenciesMap[1].srcSubpass      = 0;
            attachment_specification.DependenciesMap[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
            attachment_specification.DependenciesMap[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            attachment_specification.DependenciesMap[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            attachment_specification.DependenciesMap[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            attachment_specification.DependenciesMap[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
            attachment_specification.DependenciesMap[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        }
        m_attachment = Renderers::RenderPasses::Attachment::Create(attachment_specification);

        /* Create the actual Framebuffer */
        std::vector<VkImageView> attachment_view_collection = {};
        if (m_specification.InputColorAttachment.empty())
        {
            for (const Ref<Textures::Texture2D>& texture : m_color_attachment_collection)
            {
                const auto buffer = texture->GetImage2DBuffer();
                attachment_view_collection.push_back(buffer->GetImageViewHandle());
            }
            auto depth_texture_buffer = m_depth_attachment->GetImage2DBuffer();
            attachment_view_collection.push_back(depth_texture_buffer->GetImageViewHandle());
        }
        else
        {
            for (auto& color_buffer : m_specification.InputColorAttachment)
            {
                const auto& buffer = color_buffer.second->GetBuffer();
                attachment_view_collection.push_back(buffer.ViewHandle);
            }
        }
        m_handle = Hardwares::VulkanDevice::CreateFramebuffer(
            attachment_view_collection, m_attachment->GetHandle(), m_specification.Width, m_specification.Height, m_specification.Layers);

        /* Create Sampler */
        m_sampler = Hardwares::VulkanDevice::CreateImageSampler();
    }

    void FramebufferVNext::Resize(uint32_t width, uint32_t height)
    {
        m_specification.Width  = width;
        m_specification.Height = height;
        Dispose();
        Create();
    }

    void FramebufferVNext::Dispose()
    {
        for (Ref<Textures::Texture2D>& buffer : m_color_attachment_collection)
        {
            buffer->Dispose();
        }

        if (m_depth_attachment)
        {
            m_depth_attachment->Dispose();
        }

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

        if (m_attachment)
        {
            m_attachment->Dispose();
        }
    }

    const std::vector<Ref<Textures::Texture2D>>& FramebufferVNext::GetColorAttachmentCollection() const
    {
        return m_color_attachment_collection;
    }

    Ref<Textures::Texture2D> FramebufferVNext::GetDepthAttachment() const
    {
        return m_depth_attachment;
    }
} // namespace ZEngine::Rendering::Buffers
