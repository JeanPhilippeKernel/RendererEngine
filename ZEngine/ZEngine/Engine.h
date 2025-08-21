#pragma once
#include <Applications/GameApplication.h>
#include <EngineConfiguration.h>
#include <Event/EngineClosedEvent.h>
#include <Hardwares/VulkanDevice.h>
#include <Windows/CoreWindow.h>

namespace ZEngine
{
    struct EngineContext
    {
        Hardwares::VulkanDevicePtr Device = nullptr;
        Windows::CoreWindowPtr     Window = nullptr;
    };
    ZDEFINE_PTR(EngineContext);

    struct Engine
    {
        static void Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::WindowConfigurationPtr window_cfg_ptr, Applications::GameApplicationPtr app);
        static void Run();
        static void Deinitialize();
        static void Dispose();
        static bool OnEngineClosed(Event::EngineClosedEvent&);

    private:
        Engine()              = delete;
        Engine(const Engine&) = delete;
        ~Engine()             = delete;
    };
    ZDEFINE_PTR(Engine);
} // namespace ZEngine
