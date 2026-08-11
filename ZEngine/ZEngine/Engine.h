#pragma once
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Event/EngineClosedEvent.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Windows/CoreWindow.h>

namespace ZEngine
{
    struct EngineContext
    {
        Hardwares::VulkanDevicePtr   Device       = nullptr;
        Windows::CoreWindowPtr       Window       = nullptr;
        Core::Memory::ArenaAllocator VFSArena     = {};
        Core::VFS::IVFSContext*      VFS          = nullptr;
        Core::Memory::ArenaAllocator InputArena   = {};
        Input::InputManager*         InputManager = nullptr;
    };
    ZDEFINE_PTR(EngineContext);

    struct Engine
    {
        static void             Initialize(Core::Memory::MemoryManager* memory, Windows::WindowConfigurationPtr window_cfg_ptr, Applications::GameApplicationPtr app);
        static void             Run();
        static void             Deinitialize();
        static void             Dispose();
        static bool             OnEngineClosed(Event::EngineClosedEvent&);
        static EngineContextPtr GetContext();

        static void             MainThreadRun();
        static void             RenderThreadRun();

    private:
        Engine()              = delete;
        Engine(const Engine&) = delete;
        ~Engine()             = delete;
    };
    ZDEFINE_PTR(Engine);
} // namespace ZEngine
