#pragma once

#include <ZEngine/Core/IEventable.h>
#include <ZEngine/Core/IInitializable.h>
#include <ZEngine/Core/IRenderable.h>
#include <ZEngine/Core/IUpdatable.h>
#include <ZEngine/Helpers/IntrusivePtr.h>

namespace Tetragrama::Controllers
{
    struct IController : public ZEngine::Core::IInitializable, public ZEngine::Core::IUpdatable, public ZEngine::Core::IEventable, public ZEngine::Helpers::RefCounted
    {
        IController()  = default;
        ~IController() = default;
    };
} // namespace Tetragrama::Controllers
