#include <pch.h>
#include <Rendering/Buffers/FrameBuffers/Framebuffer.h>
#include <Logging/LoggerDefinition.h>
#include <Helpers/FrameBufferTextureHelpers.h>

namespace ZEngine::Rendering::Buffers
{
    FrameBuffer::FrameBuffer(const FrameBufferSpecification& specification) : m_specification(specification)
    {
        m_pixel_buffer = {};
        this->Resize(m_specification.Width, m_specification.Height);
    }

    FrameBuffer::~FrameBuffer()
    {
        glDeleteTextures(m_texture_color_attachments.size(), m_texture_color_attachments.data());
        glDeleteTextures(1, &m_texture_depth_attachment);

        m_texture_color_attachments.clear();
        m_texture_color_attachments.shrink_to_fit();
        m_texture_depth_attachment = 0;

        glDeleteFramebuffers(1, &m_framebuffer_identifier);
    }

    GLuint FrameBuffer::GetIdentifier() const
    {
        return m_framebuffer_identifier;
    }

    void FrameBuffer::Bind()
    {
        if (!m_is_binding)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_identifier);
            glViewport(0, 0, m_specification.Width, m_specification.Height);
            m_is_binding = true;
        }
    }

    void FrameBuffer::Unbind()
    {
        if (m_is_binding)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_is_binding = false;
        }
    }

    void FrameBuffer::ClearColorAttachments()
    {
        auto& color_attachement_specification_0 = m_specification.ColorAttachmentSpecifications[0];
        glClearTexImage(m_texture_color_attachments[0], 0, GL_RGBA, color_attachement_specification_0.ClearColorType, color_attachement_specification_0.ClearColor);

        auto& color_attachement_specification_1      = m_specification.ColorAttachmentSpecifications[1];
        int   default_clear_color                    = -1;
        color_attachement_specification_1.ClearColor = &default_clear_color;
        glClearTexImage(m_texture_color_attachments[1], 0, GL_RED_INTEGER, color_attachement_specification_1.ClearColorType, color_attachement_specification_1.ClearColor);
    }

    const FrameBufferSpecification& FrameBuffer::GetSpecification() const
    {
        return m_specification;
    }

    FrameBufferSpecification& FrameBuffer::GetSpecification()
    {
        return m_specification;
    }

    uint32_t FrameBuffer::GetTexture(uint32_t color_attachment_index) const
    {
        assert(color_attachment_index < m_texture_color_attachments.size());
        return m_texture_color_attachments[color_attachment_index];
    }

    int FrameBuffer::ReadPixelAt(int32_t pixel_pos_x, int32_t pixel_pos_y, uint32_t color_attachment_index)
    {
        assert(color_attachment_index < m_texture_color_attachments.size());

        int pixel_data;
        if (!m_is_binding)
        {
            Bind();
        }
        m_pixel_buffer.ReadPixelFrom(
            GL_COLOR_ATTACHMENT0 + color_attachment_index, pixel_pos_x, pixel_pos_y, 1, 1, GL_RED_INTEGER, GL_INT, [&pixel_data](int* const data_ptr) { pixel_data = *data_ptr; });
        Unbind();
        return pixel_data;
    }

    void FrameBuffer::Resize(uint32_t width, uint32_t height)
    {
        m_specification.Width  = (m_specification.Width != width) ? width : m_specification.Width;
        m_specification.Height = (m_specification.Height != height) ? height : m_specification.Height;

        /*Cleaunp before creating new framebuffer*/
        if (m_framebuffer_identifier)
        {
            glDeleteFramebuffers(1, &m_framebuffer_identifier);

            glDeleteTextures(m_texture_color_attachments.size(), m_texture_color_attachments.data());
            glDeleteTextures(1, &m_texture_depth_attachment);

            m_texture_color_attachments.clear();
            m_texture_color_attachments.shrink_to_fit();
            m_texture_depth_attachment = 0;
        }

#ifdef _WIN32
        glCreateFramebuffers(1, &m_framebuffer_identifier);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_identifier);
#else
        glGenFramebuffers(1, &m_framebuffer_identifier);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_identifier);
#endif

        if (!m_specification.ColorAttachmentSpecifications.empty())
        {
            m_texture_color_attachments.resize(m_specification.ColorAttachmentSpecifications.size());
            if (m_specification.Samples > 1)
            {
#ifdef _WIN32
                glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, m_specification.ColorAttachmentSpecifications.size(), m_texture_color_attachments.data());
#else
                glGenTextures(m_specification.ColorAttachmentSpecifications.size(), m_texture_color_attachments.data());
#endif
                for (auto i = 0; i < m_texture_color_attachments.size(); ++i)
                {
                    const auto& color_attachement_specification = m_specification.ColorAttachmentSpecifications[i];

                    Helpers::CreateFrameBufferTexture2DMultiSampleColorAttachment(m_texture_color_attachments[i], i,
                        (color_attachement_specification.TextureSpecification.TextureFormat == Buffers::FrameBuffers::FrameBufferColorAttachmentTextureFormat::RGBA8) ? GL_RGBA8
                                                                                                                                                                      : GL_R32I,
                        m_specification.Samples, m_specification.Width, m_specification.Height, i);
                }
            }
            else
            {
#ifdef _WIN32
                glCreateTextures(GL_TEXTURE_2D, m_specification.ColorAttachmentSpecifications.size(), m_texture_color_attachments.data());
#else
                glGenTextures(m_specification.ColorAttachmentSpecifications.size(), m_texture_color_attachments.data());
#endif
                for (auto i = 0; i < m_texture_color_attachments.size(); ++i)
                {
                    const auto& color_attachement_specification = m_specification.ColorAttachmentSpecifications[i];
                    Helpers::CreateFrameBufferTexture2DColorAttachment(m_texture_color_attachments[i], i,
                        (color_attachement_specification.TextureSpecification.TextureFormat == Buffers::FrameBuffers::FrameBufferColorAttachmentTextureFormat::RGBA8) ? GL_RGBA8
                                                                                                                                                                      : GL_R32I,
                        (color_attachement_specification.TextureSpecification.TextureFormat == Buffers::FrameBuffers::FrameBufferColorAttachmentTextureFormat::RGBA8)
                            ? GL_RGBA
                            : GL_RED_INTEGER,
                        m_specification.Width, m_specification.Height, i);
                }
            }
        }

        if (m_specification.DepthAttachmentSpecification.DepthAttachmentTextureSpecifications.TextureFormat != FrameBuffers::FrameBufferDepthAttachmentTextureFormat::None)
        {
            if (m_specification.Samples > 1)
            {
#ifdef _WIN32
                glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_texture_depth_attachment);
#else
                glGenTextures(1, &m_texture_depth_attachment);
#endif

                Helpers::CreateFrameBufferTexture2DMultiSampleDepthAttachment(
                    m_texture_depth_attachment, 0, GL_DEPTH32F_STENCIL8, m_specification.Samples, m_specification.Width, m_specification.Height, GL_DEPTH_STENCIL_ATTACHMENT);
            }
            else
            {
#ifdef _WIN32
                glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_depth_attachment);
#else
                glGenTextures(1, &m_texture_depth_attachment);
#endif
                Helpers::CreateFrameBufferTexture2DDepthAttachment(
                    m_texture_depth_attachment, 0, GL_DEPTH32F_STENCIL8, m_specification.Width, m_specification.Height, GL_DEPTH_STENCIL_ATTACHMENT);
            }
        }

        if (m_texture_color_attachments.size() > 1)
        {
            assert(m_texture_color_attachments.size() <= 4);

            GLenum buffers[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
            glDrawBuffers(m_texture_color_attachments.size(), buffers);
        }
        else if (m_texture_color_attachments.empty())
        {
            // depth pass
            glDrawBuffer(GL_NONE);
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            ZENGINE_CORE_CRITICAL("Framebuffer is incomplete")
            ZENGINE_EXIT_FAILURE();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
} // namespace ZEngine::Rendering::Buffers
