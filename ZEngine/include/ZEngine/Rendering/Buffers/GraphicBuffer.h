#pragma once
#include <vector>
#include <type_traits>
#include <algorithm>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Buffers
{

    template <typename T>
    class GraphicBuffer
    {
    public:
        explicit GraphicBuffer() : m_data()
        {
            m_byte_size = sizeof(T);
        }

        explicit GraphicBuffer(const T& data) : m_data(data)
        {
            m_byte_size = sizeof(T);
            m_data      = data;
        }

        virtual ~GraphicBuffer() = default;

        virtual void Bind() const   = 0;
        virtual void Unbind() const = 0;

        virtual void SetData(const T& data)
        {
            this->m_byte_size = sizeof(T);
            this->m_data      = data;
        }

        virtual void SetData(T&& data)
        {
            this->m_data      = std::move(data);
            this->m_byte_size = sizeof(T);
        }

        virtual const T& GetData() const
        {
            return m_data;
        }

        virtual size_t GetByteSize() const
        {
            return m_byte_size;
        }

        virtual size_t GetDataSize() const
        {
            return m_data_size;
        }

    protected:
        size_t m_byte_size{0};
        size_t m_data_size{0};
        T      m_data;
    };

    template <>
    class GraphicBuffer<std::vector<float>>
    {
    public:
        explicit GraphicBuffer() : m_data()
        {
            m_byte_size = sizeof(float);
        }

        explicit GraphicBuffer(const std::vector<float>& data) : m_data(data)
        {
            m_byte_size = sizeof(float) * data.size();
            m_data      = data;
        }

        virtual ~GraphicBuffer() = default;

        virtual void Bind() const   = 0;
        virtual void Unbind() const = 0;

        virtual void SetData(const std::vector<float>& data)
        {
            this->m_byte_size = sizeof(float) * data.size();
            this->m_data      = data;
        }

        virtual void SetData(std::vector<float>&& data)
        {
            this->m_data      = std::move(data);
            this->m_byte_size = sizeof(float) * m_data.size();
        }

        virtual const std::vector<float>& GetData() const
        {
            return m_data;
        }

        virtual size_t GetByteSize() const
        {
            return m_byte_size;
        }

        virtual size_t GetDataSize() const
        {
            return m_data_size;
        }

    protected:
        size_t             m_byte_size{0};
        size_t             m_data_size{0};
        std::vector<float> m_data;
    };

    template <>
    class GraphicBuffer<std::vector<uint32_t>>
    {
    public:
        explicit GraphicBuffer() : m_data()
        {
            m_byte_size = sizeof(uint32_t);
        }

        explicit GraphicBuffer(const std::vector<uint32_t>& data) : m_data(data)
        {
            m_byte_size = sizeof(uint32_t) * data.size();
            m_data      = data;
        }

        virtual ~GraphicBuffer() = default;

        virtual void Bind() const   = 0;
        virtual void Unbind() const = 0;

        virtual void SetData(const std::vector<uint32_t>& data)
        {
            this->m_byte_size = sizeof(uint32_t) * data.size();
            this->m_data      = data;
        }

        virtual void SetData(std::vector<uint32_t>&& data)
        {
            this->m_data      = std::move(data);
            this->m_byte_size = sizeof(uint32_t) * m_data.size();
        }

        virtual const std::vector<uint32_t>& GetData() const
        {
            return m_data;
        }

        virtual size_t GetByteSize() const
        {
            return m_byte_size;
        }

        virtual size_t GetDataSize() const
        {
            return m_data_size;
        }

    protected:
        size_t             m_byte_size{0};
        size_t             m_data_size{0};
        std::vector<uint32_t> m_data;
    };
} // namespace ZEngine::Rendering::Buffers
