#pragma once

#include <Core/CoreEvent.h>
#include <fmt/format.h>

namespace ZEngine::Windows::Events
{

    class TextInputEvent : public Core::CoreEvent
    {
    public:
        TextInputEvent(std::string_view content) : m_text(content) {}

        EVENT_TYPE(TextInput)
        EVENT_CATEGORY(Keyboard | Core::EventCategory::Input)

        virtual Core::EventType GetType() const override
        {
            return GetStaticType();
        }

        virtual int GetCategory() const override
        {
            return GetStaticCategory();
        }

        virtual std::string ToString() const override
        {
            return fmt::format("TextInputEvent : {0}", this->m_text);
        }

        std::string_view GetText() const
        {
            return m_text;
        }

    protected:
        std::string m_text;
    };
} // namespace ZEngine::Windows::Events
