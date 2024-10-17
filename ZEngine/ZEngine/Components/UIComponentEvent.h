#pragma once
#include <Event/CoreEvent.h>

namespace ZEngine::Components::UI::Event
{

    class UIComponentEvent : public ZEngine::Event::CoreEvent
    {
    public:
        UIComponentEvent()          = default;
        virtual ~UIComponentEvent() = default;

        EVENT_CATEGORY(UserInterfaceComponent)

        virtual std::string ToString() const override
        {
            return "UIComponentEvent";
        }
    };
} // namespace ZEngine::Components::UI::Event
