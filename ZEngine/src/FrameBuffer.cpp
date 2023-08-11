#include <pch.h>
#include <Rendering/Buffers/FrameBuffers/Framebuffer.h>
#include <Logging/LoggerDefinition.h>
#include <Helpers/RendererHelper.h>

namespace ZEngine::Rendering::Buffers
{
    FramebufferVNext::FramebufferVNext(const FrameBufferSpecificationVNext& specification) : m_framebuffer_specification(specification)
    {
        this->m_width  = specification.Width;
        this->m_height = specification.Height;
        this->m_layers = specification.Layers;

        uint32_t                                      image_format_index               = 0;
        Renderers::RenderPasses::SubPassSpecification attachment_subpass_specification = {};
        for (auto image_format : specification.AttachmentSpecifications.FormatCollection)
        {
            if (image_format == Buffers::FrameBuffers::ImageFormat::R8G8B8A8_UNORM)
            {
                auto color_attachment = CreateRef<Image2DBuffer>(
                    m_width,
                    m_height,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT);

                m_color_attachment_collection.push_back(color_attachment);

                VkAttachmentDescription color_attachment_description = {};
                color_attachment_description.format                  = VK_FORMAT_R8G8B8A8_UNORM;
                color_attachment_description.samples                 = VK_SAMPLE_COUNT_1_BIT;
                color_attachment_description.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
                color_attachment_description.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
                color_attachment_description.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                color_attachment_description.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                color_attachment_description.initialLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                color_attachment_description.finalLayout             = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                m_attachment_specification.ColorAttachements.push_back(std::move(color_attachment_description));

                VkAttachmentReference color_attachment_reference = {};
                color_attachment_reference.attachment            = image_format_index;
                color_attachment_reference.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                attachment_subpass_specification.ColorAttachementReferences.push_back(std::move(color_attachment_reference));
            }
            else if (image_format == Buffers::FrameBuffers::ImageFormat::DEPTH_STENCIL)
            {
                auto depth_format  = Helpers::FindDepthFormat();
                m_depth_attachment = CreateRef<Image2DBuffer>(m_width, m_height, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

                VkAttachmentDescription depth_attachment_description = {};
                depth_attachment_description.format                  = depth_format;
                depth_attachment_description.samples                 = VK_SAMPLE_COUNT_1_BIT;
                depth_attachment_description.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depth_attachment_description.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
                depth_attachment_description.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depth_attachment_description.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depth_attachment_description.initialLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                depth_attachment_description.finalLayout             = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                m_attachment_specification.ColorAttachements.push_back(std::move(depth_attachment_description));

                VkAttachmentReference depth_attachment_reference                  = {};
                depth_attachment_reference.attachment                             = image_format_index;
                depth_attachment_reference.layout                                 = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                attachment_subpass_specification.DepthStencilAttachementReference = std::move(depth_attachment_reference);
            }
            image_format_index++;
        }

        /*RenderPass definition*/
        attachment_subpass_specification.SubpassDescription                   = {};
        attachment_subpass_specification.SubpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        m_attachment_specification.SubpassSpecifications.push_back(attachment_subpass_specification);

        /*Subpass definition*/
        std::vector<VkSubpassDependency> dependencies(2);
        dependencies[0].srcSubpass                      = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass                      = 0;
        dependencies[0].srcStageMask                    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask                    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags                 = VK_DEPENDENCY_BY_REGION_BIT;
        dependencies[1].srcSubpass                      = 0;
        dependencies[1].dstSubpass                      = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask                    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask                    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags                 = VK_DEPENDENCY_BY_REGION_BIT;
        m_attachment_specification.SubpassDependencies  = dependencies;

        m_renderpass = Helpers::CreateRenderPass(m_attachment_specification);

        /* Create the actual Framebuffer */
        std::vector<VkImageView> attachment_view_collection = {};
        for (const Ref<Image2DBuffer>& buffer : m_color_attachment_collection)
        {
            attachment_view_collection.push_back(buffer->GetImageViewHandle());
        }
        attachment_view_collection.push_back(m_depth_attachment->GetImageViewHandle());

        this->m_handle = Helpers::CreateFramebuffer(attachment_view_collection, m_renderpass, specification.Width, specification.Height, specification.Layers);

        /* Create Sampler */
        this->m_sampler = Helpers::CreateTextureSampler();
    }

    FramebufferVNext::~FramebufferVNext()
    {
        Dispose();
    }

    VkRenderPass FramebufferVNext::GetRenderPass() const
    {
        return m_renderpass;
    }

    VkFramebuffer FramebufferVNext::GetHandle() const
    {
        return m_handle;
    }

    VkSampler FramebufferVNext::GetSample() const
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

    void FramebufferVNext::Dispose()
    {
        for (Ref<Image2DBuffer>& buffer : m_color_attachment_collection)
        {
            buffer->Dispose();
        }

        m_depth_attachment->Dispose();

        if (m_sampler)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::SAMPLER, m_sampler);
            m_sampler = VK_NULL_HANDLE;
        }

        if (m_handle)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::FRAMEBUFFER, m_handle);
            m_handle  = VK_NULL_HANDLE;
        }

        if (m_renderpass)
        {
            Hardwares::VulkanDevice::EnqueueForDeletion(Rendering::DeviceResourceType::RENDERPASS, m_renderpass);
            m_renderpass = VK_NULL_HANDLE;
        }
    }
} // namespace ZEngine::Rendering::Buffers
