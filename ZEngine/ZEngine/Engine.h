#pragma once
#include <EngineConfiguration.h>
#include <Event/EngineClosedEvent.h>
#include <Window/CoreWindow.h>

namespace ZEngine
{
    struct Engine
    {
        Engine()              = delete;
        Engine(const Engine&) = delete;
        ~Engine()             = delete;

        static void                    Initialize(const EngineConfiguration&, const Ref<ZEngine::Window::CoreWindow>&);
        static void                    Run();
        static Ref<Window::CoreWindow> GetWindow();
        static void                    Deinitialize();
        static void                    Dispose();
        static bool                    OnEngineClosed(Event::EngineClosedEvent&);
    };
} // namespace ZEngine
