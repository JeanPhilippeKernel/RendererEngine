#pragma once

#include <Core/CoreEvent.h>

namespace ZEngine::Core
{

    struct IEventable
    {
        IEventable()          = default;
        virtual ~IEventable() = default;

        virtual bool OnEvent(CoreEvent&) = 0;
    };
} // namespace ZEngine::Core
