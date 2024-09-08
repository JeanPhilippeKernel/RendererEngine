#pragma once

#include <Core/IEventable.h>
#include <Core/IInitializable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Helpers/IntrusivePtr.h>

namespace ZEngine::Controllers
{
    struct IController : public Core::IInitializable, public Core::IUpdatable, public Core::IEventable, public Helpers::RefCounted
    {
        IController()  = default;
        ~IController() = default;
    };
} // namespace ZEngine::Controllers
