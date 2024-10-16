#pragma once
#include <Core/IGraphicObject.h>
#include <Rendering/Buffers/BufferLayout.h>
#include <Rendering/Buffers/GraphicBuffer.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Buffers
{

    template <typename T>
    class PixelBuffer : public Core::IGraphicObject
    {
    public:
        PixelBuffer()
        {
#ifdef _WIN32
            glCreateBuffers(1, &m_pixel_buffer_id);
#else
            glGenBuffers(1, &m_pixel_buffer_id);
#endif
        }

        ~PixelBuffer()
        {
            glDeleteBuffers(1, &m_pixel_buffer_id);
        }

        void ReadPixelFrom(
            uint32_t                             buffer_source,
            int                                  x,
            int                                  y,
            uint32_t                             width,
            uint32_t                             height,
            GLenum                               format,
            GLenum                               type,
            const std::function<void(T* const)>& read_pixel_callback)
        {
            glReadBuffer(buffer_source);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pixel_buffer_id);
            glReadPixels(x, y, width, height, format, type, NULL);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pixel_buffer_id);
            T* buffer_ptr = (T*) glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
            if (buffer_ptr)
            {
                read_pixel_callback(buffer_ptr);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }

            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }

        GLuint GetIdentifier() const override
        {
            return m_pixel_buffer_id;
        }

    private:
        GLuint m_pixel_buffer_id;
    };
} // namespace ZEngine::Rendering::Buffers
