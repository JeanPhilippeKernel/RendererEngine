#pragma once
#include <vector>
#include <string>
#include <type_traits>
#include <initializer_list>
#include <typeinfo>
#include <string_view>

namespace ZEngine::Rendering::Buffers::Layout
{

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_class_v<T>>>
    class ElementLayout
    {
    public:
        explicit ElementLayout(size_t count = 0, std::string name = "", bool normalized = false, uint32_t format = 0 /* vulkan attrib description format*/)
            : m_name(name), m_count(count), m_size(count * sizeof(T)), m_normalized(normalized), m_data_type(typeid(T).name()), m_format(format)
        {
        }

        ~ElementLayout() = default;

    public:
        virtual std::string_view GetName() const
        {
            return m_name;
        }

        virtual std::string_view GetDataType() const
        {
            return m_data_type;
        }

        virtual bool GetNormalized() const
        {
            return m_normalized;
        }

        virtual size_t GetOffset() const
        {
            return m_offset;
        }

        virtual size_t GetSize() const
        {
            return m_size;
        }

        virtual size_t GetCount() const
        {
            return m_count;
        }

        virtual void SetOffset(size_t value)
        {
            m_offset = value;
        }

        virtual uint32_t GetFormat() const
        {
            return m_format;
        }

    protected:
        std::string m_name;
        size_t      m_size{0};
        size_t      m_offset{0};
        size_t      m_count{0};
        bool        m_normalized{false};
        std::string m_data_type;
        uint32_t    m_format;
    };

    template <typename T>
    class BufferLayout
    {
    public:
        explicit BufferLayout() = default;

        explicit BufferLayout(std::initializer_list<ElementLayout<T>>&& collections) : m_elements(std::move(collections))
        {
            ComputeOffset();
        }

        explicit BufferLayout(const std::initializer_list<ElementLayout<T>>& collections) : m_elements(collections)
        {
            ComputeOffset();
        }

        std::vector<ElementLayout<T>>& GetElementLayout()
        {
            return m_elements;
        }

        const std::vector<ElementLayout<T>>& GetElementLayout() const
        {
            return m_elements;
        }

        size_t GetStride() const
        {
            size_t x{0};
            for (const auto& element : m_elements)
            {
                x += element.GetSize();
            }

            return x;
        }


        void ComputeOffset()
        {
            auto start = std::next(std::begin(m_elements)); // We start at the second element since the offset of this first element should be zero
            auto end   = std::end(m_elements);

            while (start != end)
            {
                auto current = std::prev(start);
                auto value   = current->GetSize() + current->GetOffset();
                start->SetOffset(value);

                start = std::next(start);
            }
        }

    private:
        std::vector<ElementLayout<T>> m_elements;
    };
} // namespace ZEngine::Rendering::Buffers::Layout
