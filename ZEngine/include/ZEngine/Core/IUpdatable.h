#pragma once
#include <Core/TimeStep.h>

namespace ZEngine::Core {

    struct IUpdatable {
        IUpdatable()          = default;
        virtual ~IUpdatable() = default;

        virtual void Update(TimeStep dt) = 0;
    };
} // namespace ZEngine::Core
