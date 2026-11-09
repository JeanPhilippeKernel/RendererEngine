#pragma once
#include <ZEngine/Core/CoreEvent.h>

namespace Tetragrama::Tests
{
    // The fixed-fixture runner is not an interactive editor. Native window
    // lifecycle/layout events still exercise the ordinary editor path.
    constexpr bool IsSceneLifecycleInteractiveEvent(ZEngine::Core::EventType type)
    {
        switch (type)
        {
            case ZEngine::Core::EventType::KeyPressed:
            case ZEngine::Core::EventType::KeyReleased:
            case ZEngine::Core::EventType::TextInput:
            case ZEngine::Core::EventType::MouseButtonPressed:
            case ZEngine::Core::EventType::MouseButtonReleased:
            case ZEngine::Core::EventType::MouseMoved:
            case ZEngine::Core::EventType::MouseWheel:
                return true;
            default:
                return false;
        }
    }
} // namespace Tetragrama::Tests
