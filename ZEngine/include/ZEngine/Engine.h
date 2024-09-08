#pragma once

#include <Core/IEventable.h>
#include <Core/IInitializable.h>
#include <Core/IRenderable.h>
#include <Core/IUpdatable.h>
#include <Core/TimeStep.h>
#include <EngineConfiguration.h>
#include <Event/EngineClosedEvent.h>
#include <Event/EventDispatcher.h>
#include <Event/KeyPressedEvent.h>
#include <Event/KeyReleasedEvent.h>
#include <Window/CoreWindow.h>
#include <ZEngineDef.h>

namespace ZEngine
{
    struct Engine
    {
        Engine()              = delete;
        Engine(const Engine&) = delete;
        ~Engine()             = delete;

        static void Initialize(const EngineConfiguration&);

        static void ProcessEvent();
        static void Update(Core::TimeStep delta_time);
        static void Render();

        static void Start();

        static Core::TimeStep                   GetDeltaTime();
        static Ref<ZEngine::Window::CoreWindow> GetWindow();

        static void Deinitialize();
        static void Dispose();

        static bool OnEngineClosed(Event::EngineClosedEvent&);

    private:
        static void                             Run();
        static bool                             m_request_terminate;
        static float                            m_last_frame_time;
        static Core::TimeStep                   m_delta_time;
        static Ref<ZEngine::Window::CoreWindow> m_window;
    };

    Engine* CreateEngine(const EngineConfiguration&);
} // namespace ZEngine
