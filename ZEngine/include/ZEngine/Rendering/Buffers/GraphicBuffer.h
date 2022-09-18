#pragma once
#include <vector>
#include <algorithm>
#include <iterator>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Buffers {

    template <typename T>
    class GraphicBuffer {
    public:
        explicit GraphicBuffer() : m_data() {}

        explicit GraphicBuffer(const std::vector<T>& data) : m_data(data), m_data_size(data.size()) {
            m_byte_size = m_data_size * sizeof(T);
        }

        virtual ~GraphicBuffer() = default;

        virtual void Bind() const   = 0;
        virtual void Unbind() const = 0;

        virtual void SetData(const std::vector<T>& data) {
            if (this->m_data_size > 0) {
                m_data.clear();
            }

            std::copy(std::begin(data), std::end(data), std::back_inserter(m_data));
            this->m_data_size = data.size();
            this->m_byte_size = m_data_size * sizeof(T);
        }

        virtual void SetData(std::vector<T>&& data) {
            if (this->m_data_size > 0) {
                m_data.clear();
            }

            this->m_data      = std::move(data);
            this->m_data_size = data.size();
            this->m_byte_size = m_data_size * sizeof(T);
        }

        virtual const std::vector<T>& GetData() const {
            return m_data;
        }

        virtual size_t GetByteSize() const {
            return m_byte_size;
        }

        virtual size_t GetDataSize() const {
            return m_data_size;
        }

    protected:
        size_t                  m_byte_size{0};
        size_t                  m_data_size{0};
        std::vector<T>          m_data;
    };
} // namespace ZEngine::Rendering::Buffers
