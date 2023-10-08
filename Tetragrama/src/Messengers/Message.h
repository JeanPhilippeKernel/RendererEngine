#pragma once
#include <array>

namespace Tetragrama::Messengers
{
    struct EmptyMessage
    {
    };

    template <typename T>
    struct GenericMessage : public EmptyMessage
    {
        GenericMessage() {}
        GenericMessage(const GenericMessage& rhs)
        {
            m_value = rhs.m_value;
        }
        GenericMessage(const T& value) : m_value(value) {}
        GenericMessage(T&& value) : m_value(std::move(value)) {}
        virtual ~GenericMessage() {}

        T& GetValue()
        {
            return m_value;
        }

        const T& GetValue() const
        {
            return m_value;
        }

        void SetValue(T value)
        {
            m_value = value;
        }

        void SetValue(const T& value)
        {
            m_value = value;
        }

        void SetValue(T&& value)
        {
            m_value = std::move(value);
        }

    private:
        T m_value;
    };

    template <typename T>
    struct PointerValueMessage : public EmptyMessage
    {
        PointerValueMessage() {}
        PointerValueMessage(PointerValueMessage&& rhs)
        {
            m_value     = std::move(rhs.m_value);
            rhs.m_value = nullptr;
        }
        PointerValueMessage(T* const value) : m_value(value) {}
        virtual ~PointerValueMessage() {}

        T* GetValue()
        {
            return m_value;
        }

        const T* GetValue() const
        {
            return m_value;
        }

        void SetValue(T* const value)
        {
            m_value = value;
        }

    private:
        T* m_value{nullptr};
    };

    template <typename T, size_t N>
    struct ArrayValueMessage : public EmptyMessage
    {
        ArrayValueMessage() {}
        ArrayValueMessage(const ArrayValueMessage& rhs)
        {
            m_value = rhs.m_value;
        }
        ArrayValueMessage(const std::array<T, N>& value) : m_value(value) {}
        ArrayValueMessage(std::array<T, N>&& value) : m_value(std::move(value)) {}
        virtual ~ArrayValueMessage() {}

        std::array<T, N>& GetValue()
        {
            return m_value;
        }

        const std::array<T, N>& GetValue() const
        {
            return m_value;
        }

        void SetValue(std::array<T, N> value)
        {
            m_value = value;
        }

        void SetValue(const std::array<T, N>& value)
        {
            m_value = value;
        }

        void SetValue(std::array<T, N>&& value)
        {
            m_value = std::move(value);
        }

    private:
        std::array<T, N> m_value;
    };
} // namespace Tetragrama::Messengers
