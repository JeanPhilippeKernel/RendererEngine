#pragma once
#include <ZEngine/Event/CoreEvent.h>

namespace Tetragrama::Components::Event
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
} // namespace Tetragrama::Components::Event
