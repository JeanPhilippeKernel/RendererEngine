#pragma once
#include <EngineConfiguration.h>
#include <Event/EngineClosedEvent.h>
#include <Helpers/IntrusivePtr.h>
#include <Windows/CoreWindow.h>

namespace ZEngine
{
    struct Engine
    {
        static void                              Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const EngineConfiguration&, const Helpers::Ref<ZEngine::Windows::CoreWindow>&);
        static void                              Run();
        static Helpers::Ref<Windows::CoreWindow> GetWindow();
        static void                              Deinitialize();
        static void                              Dispose();
        static bool                              OnEngineClosed(Event::EngineClosedEvent&);

    private:
        Engine()              = delete;
        Engine(const Engine&) = delete;
        ~Engine()             = delete;
    };
} // namespace ZEngine
