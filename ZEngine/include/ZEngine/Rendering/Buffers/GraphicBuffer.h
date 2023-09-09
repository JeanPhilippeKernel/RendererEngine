#pragma once

namespace ZEngine::Rendering::Buffers
{
    struct IGraphicBuffer
    {
        IGraphicBuffer()          = default;
        virtual ~IGraphicBuffer() = default;

        virtual inline size_t GetByteSize() const
        {
            return m_byte_size;
        }

    protected:
        size_t m_byte_size{0};
    };
} // namespace ZEngine::Rendering::Buffers
