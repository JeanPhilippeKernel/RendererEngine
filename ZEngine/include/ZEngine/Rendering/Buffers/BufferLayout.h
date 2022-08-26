#pragma once
#include <vector>
#include <string>
#include <type_traits>
#include <initializer_list>
#include <typeinfo>

#include <ZEngineDef.h>

namespace ZEngine::Rendering::Buffers::Layout {

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_class_v<T>>>
    class ElementLayout {
    public:
        explicit ElementLayout(size_t Count = 0, std::string name = "", bool normalized = false)
            : m_name(name), m_count(Count), m_size(Count * sizeof(T)), m_normalized(normalized), m_data_type(typeid(T).name()) {}

        ~ElementLayout() = default;

    public:
        virtual const std::string& GetName() const {
            return m_name;
        }

        virtual const std::string& GetDataType() const {
            return m_data_type;
        }

        virtual bool GetNormalized() const {
            return m_normalized;
        }

        virtual size_t GetOffset() const {
            return m_offset;
        }

        virtual size_t GetSize() const {
            return m_size;
        }

        virtual size_t GetCount() const {
            return m_count;
        }

        virtual void SetOffset(size_t value) {
            m_offset = value;
        }

    protected:
        std::string m_name;
        size_t      m_size{0};
        size_t      m_offset{0};
        size_t      m_count{0};
        bool        m_normalized{false};
        std::string m_data_type;
    };

    template <typename T>
    class BufferLayout {
    public:
        explicit BufferLayout() = default;

        explicit BufferLayout(std::initializer_list<ElementLayout<T>>&& collections) : m_elements(std::move(collections)) {}

        explicit BufferLayout(const std::initializer_list<ElementLayout<T>>& collections) : m_elements(collections) {}

        std::vector<ElementLayout<T>>& GetElementLayout() {
            return m_elements;
        }

        const std::vector<ElementLayout<T>>& GetElementLayout() const {
            return m_elements;
        }

        size_t GetStride() const {
            size_t x{0};
            for (const auto& element : m_elements) {
                x += element.GetSize();
            }

            return x;
        }

    private:
        std::vector<ElementLayout<T>> m_elements;
    };
} // namespace ZEngine::Rendering::Buffers::Layout
