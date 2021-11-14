#pragma once
#include <Core/IGraphicObject.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>

namespace ZEngine::Rendering::Buffers {

    class FrameBuffer : public Core::IGraphicObject {
    public:
        FrameBuffer(const FrameBufferSpecification&);
        ~FrameBuffer();

        virtual GLuint GetIdentifier() const override;

        void Resize();
        void Resize(unsigned int width, unsigned int height) = delete;

        void Bind() const;
        void Unbind() const;

        const FrameBufferSpecification& GetSpecification() const { return m_specification; }
        FrameBufferSpecification& GetSpecification() { return m_specification; }

        unsigned int GetTexture() const { return m_texture_color_attachment_identifier; }

    private:
        GLuint m_framebuffer_identifier;
        GLuint m_texture_color_attachment_identifier;
        GLuint m_texture_color_depth_attachment;
        FrameBufferSpecification m_specification;
    };
}
