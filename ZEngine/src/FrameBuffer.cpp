#include <Rendering/Buffers/FrameBuffers/Framebuffer.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine::Rendering::Buffers {

    FrameBuffer::FrameBuffer(const FrameBufferSpecification& specification)
        : m_specification(specification)
    {

        this->Resize();
    }
    
    FrameBuffer::~FrameBuffer() {
        glDeleteFramebuffers(1, &m_framebuffer_identifier);
    }

    GLuint FrameBuffer::GetIdentifier() const {
        return m_framebuffer_identifier;
    }

    void FrameBuffer::Bind() const { 
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_identifier); 
    }
    
    void FrameBuffer::Unbind() const { 
        glBindFramebuffer(GL_FRAMEBUFFER, 0); 
    }

    void FrameBuffer::Resize() {
#ifdef _WIN32
        glCreateFramebuffers(1, &m_framebuffer_identifier);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_identifier);

        glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_color_attachment_identifier);
        glBindTextureUnit(0, m_texture_color_attachment_identifier);
        glTextureStorage2D(m_texture_color_attachment_identifier, 1, GL_RGBA8, m_specification.Width, m_specification.Height);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_specification.Width, m_specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTextureParameteri(m_texture_color_attachment_identifier, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_texture_color_attachment_identifier, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture_color_attachment_identifier, 0);

        if (m_specification.HasStencil && m_specification.HasDepth) {
            glCreateTextures(GL_TEXTURE_2D, 1, &m_texture_color_depth_attachment);
            glBindTextureUnit(0, m_texture_color_depth_attachment);
            glTextureStorage2D(m_texture_color_depth_attachment, 1, GL_DEPTH32F_STENCIL8, m_specification.Width, m_specification.Height);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_texture_color_depth_attachment, 0);
        }
#else
        glGenFramebuffers(1, &m_framebuffer_identifier);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer_identifier);

        glGenTextures(1, &m_texture_color_attachment_identifier);
        glBindTexture(GL_TEXTURE_2D, m_texture_color_attachment_identifier);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_specification.Width, m_specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture_color_attachment_identifier, 0);

        if (m_specification.HasStencil && m_specification.HasDepth) {
            glGenTextures(1, &m_texture_color_depth_attachment);
            glBindTexture(GL_TEXTURE_2D, m_texture_color_depth_attachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_specification.Width, m_specification.Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_texture_color_depth_attachment, 0);
        }

#endif

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            Z_ENGINE_CORE_CRITICAL("Framebuffer is incomplete");
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}
