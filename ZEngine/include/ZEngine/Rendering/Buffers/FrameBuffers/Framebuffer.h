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

    private:
        GLuint m_framebuffer_identifier;
        GLuint m_texture_identifier;
        GLuint m_render_buffer_identifier;
        FrameBufferSpecification m_specification;
    };
}