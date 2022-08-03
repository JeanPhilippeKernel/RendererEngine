#pragma once

namespace Tetragrama::Messengers {
    struct EmptyMessage {};

    template <typename T>
    struct GenericMessage : public EmptyMessage {
        GenericMessage() {}
        GenericMessage(const GenericMessage& rhs) {
            m_value = rhs.m_value;
        }
        GenericMessage(const T& value) : m_value(value) {}
        GenericMessage(T&& value) : m_value(std::move(value)) {}
        virtual ~GenericMessage() {}

        T& GetValue() {
            return m_value;
        }

        const T& GetValue() const {
            return m_value;
        }

        void SetValue(T value) {
            m_value = value;
        }

        void SetValue(const T& value) {
            m_value = value;
        }

        void SetValue(T&& value) {
            m_value = std::move(value);
        }

    private:
        T m_value;
    };

    template <typename T>
    struct PointerValueMessage : public EmptyMessage {
        PointerValueMessage() {}
        PointerValueMessage(PointerValueMessage&& rhs) {
            m_value     = std::move(rhs.m_value);
            rhs.m_value = nullptr;
        }
        PointerValueMessage(T* const value) : m_value(value) {}
        virtual ~PointerValueMessage() {}

        T* GetValue() {
            return m_value;
        }

        const T* GetValue() const {
            return m_value;
        }

        void SetValue(T* const value) {
            m_value = value;
        }

    private:
        T* m_value{nullptr};
    };
} // namespace Tetragrama::Messengers
