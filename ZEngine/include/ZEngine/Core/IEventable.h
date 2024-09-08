#pragma once

#include <Event/CoreEvent.h>

namespace ZEngine::Core
{

    struct IEventable
    {
        IEventable()          = default;
        virtual ~IEventable() = default;

        virtual bool OnEvent(Event::CoreEvent&) = 0;
    };
} // namespace ZEngine::Core
