#pragma once
#include <ZEngine/Core/CoreEvent.h>

namespace Tetragrama::Components::Event
{

    class UIComponentEvent : public ZEngine::Core::CoreEvent
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
