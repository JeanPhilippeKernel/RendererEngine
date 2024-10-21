#pragma once
#include <Core/CoreEvent.h>
#include <fmt/format.h>

namespace ZEngine::Event
{
    class EngineClosedEvent : public Core::CoreEvent
    {
    public:
        EngineClosedEvent(std::string_view r) : CoreEvent(), m_reason(r)
        {
            m_name = "EngineClosed";
        }
        ~EngineClosedEvent() = default;

        void SetReason(std::string_view value)
        {
            m_reason = value;
        }

        Core::EventType GetType() const override
        {
            return GetStaticType();
        }

        int GetCategory() const override
        {
            return GetStaticCategory();
        }

        std::string ToString() const override
        {
            return fmt::format("{}: {}", m_name, m_reason);
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(EngineClosed)

    private:
        std::string m_reason{};
    };
} // namespace ZEngine::Event
