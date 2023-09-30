#pragma once

namespace ZEngine::Rendering::Buffers
{
    struct IGraphicBuffer
    {
        IGraphicBuffer()
        {
            m_last_byte_size = m_byte_size;
        }

        virtual ~IGraphicBuffer() = default;

        virtual inline size_t GetByteSize() const
        {
            return m_byte_size;
        }

        virtual bool HasBeenResized() const
        {
            return m_last_byte_size != m_byte_size;
        }

        virtual void* GetNativeBufferHandle() const = 0;

    protected:
        size_t m_byte_size{0};
        size_t m_last_byte_size{0};
    };
} // namespace ZEngine::Rendering::Buffers
