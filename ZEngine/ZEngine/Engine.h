#pragma once
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/WorldCommands.h>
#include <ZEngine/ECS/WorldTick.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Event/EngineClosedEvent.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Windows/CoreWindow.h>

namespace ZEngine
{
    struct EngineContext
    {
        // Sub-arenas (large structs — grouped together to avoid pointer/arena interleaving)
        Core::Memory::ArenaAllocator VFSArena      = {};
        Core::Memory::ArenaAllocator AssetArena    = {};
        Core::Memory::ArenaAllocator InputArena    = {};
        Core::Memory::ArenaAllocator ECSArena      = {};

        // Pointers (8 bytes each — grouped to pack cleanly)
        Hardwares::VulkanDevicePtr   Device        = nullptr;
        Windows::CoreWindowPtr       Window        = nullptr;
        Core::VFS::IVFSContext*      VFS           = nullptr;
        Input::InputManager*         InputManager  = nullptr;
        ECS::Scene*                  Scene         = nullptr;
        ECS::ActorManager*           ActorManager  = nullptr;
        ECS::WorldCommands*          WorldCommands = nullptr;
        ECS::WorldTick*              WorldTick     = nullptr;
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
