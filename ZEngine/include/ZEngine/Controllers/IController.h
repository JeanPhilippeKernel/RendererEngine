#pragma once

#include <Core/IUpdatable.h>
#include <Core/IRenderable.h>
#include <Core/IEventable.h>
#include <Core/IInitializable.h>

namespace ZEngine::Controllers
{
    struct IController : public Core::IInitializable, public Core::IUpdatable, public Core::IEventable
    {
        IController()  = default;
        ~IController() = default;
    };
} // namespace ZEngine::Controllers
