#pragma once
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSDirectoryCache.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Core/VFS/VFSWatchEvent.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/WorldCommands.h>
#include <ZEngine/ECS/WorldTick.h>
#include <ZEngine/EngineConfiguration.h>
#include <ZEngine/Event/EngineClosedEvent.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Importers/EnvironmentMapImporter.h>
#include <ZEngine/Importers/FbxImporter.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <ZEngine/Importers/ImportCoordinator.h>
#include <ZEngine/Importers/TextureImporter.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <ZEngine/ZEngineDef.h>
#include <thread>

namespace ZEngine
{
    struct EngineContext
    {
        // VFS backend for engine-owned assets (Shaders/, Settings/).
        // Mounted at /ZodiacEngine with priority -1 so the workspace backend
        // (priority 0) takes precedence for any overlapping paths.
        Core::VFS::VFSDiskBackend         EngineAssetsBackend             = {};

        // Writable project-owned backend mounted specifically at /ZodiacEngine/cache.
        // It is distinct from packaged engine assets, which may be read-only.
        Core::VFS::VFSDiskBackend         PipelineCacheBackend            = {};

        // Project-wide directory listing cache and async tree walker — populates
        // AssetRegistry from disk once at startup (VFSContext::ScanProject) and
        // backs the file watcher's rescan-on-change.
        Core::VFS::VFSDirectoryCache      VFSDirectoryCache               = {};
        Core::VFS::VFSScanner             VFSScanner                      = {};

        // Import pipeline — registered with ImportCoordinator; each carves its own
        // sub-arena from ImportPipelineArena.
        Importers::GltfImporter           GltfImporter                    = {};
        Importers::FbxImporter            FbxImporter                     = {};
        Importers::AssimpImporter         AssimpImporter                  = {};
        Importers::EnvironmentMapImporter EnvironmentMapImporter          = {};
        Importers::TextureImporter        TextureImporter                 = {};

        // Sub-arenas (large structs — grouped together to avoid pointer/arena interleaving)
        Core::Memory::ArenaAllocator      VFSArena                        = {};
        Core::Memory::ArenaAllocator      AssetArena                      = {};
        Core::Memory::ArenaAllocator      InputArena                      = {};
        Core::Memory::ArenaAllocator      ECSArena                        = {};
        Core::Memory::ArenaAllocator      ImportPipelineArena             = {};
        Core::Memory::ArenaAllocator      UIContextArena                  = {};

        // Pointers (8 bytes each — grouped to pack cleanly)
        Hardwares::VulkanDevicePtr        Device                          = nullptr;
        Windows::CoreWindowPtr            Window                          = nullptr;
        Core::VFS::IVFSContext*           VFS                             = nullptr;
        Input::InputManager*              InputManager                    = nullptr;
        ECS::Scene*                       Scene                           = nullptr;
        ECS::ActorManager*                ActorManager                    = nullptr;
        ECS::WorldCommands*               WorldCommands                   = nullptr;
        ECS::WorldTick*                   WorldTick                       = nullptr;
        Importers::ImportCoordinator*     ImportCoordinator               = nullptr;
        Rendering::RenderResourceManager* RenderResourceManager           = nullptr;
        Applications::GameApplicationPtr  App                             = nullptr;

        // Smoothed delta time — 8-sample rolling average measured in the render thread
        // between consecutive EndFrame() calls (includes vsync wait).
        // Written by the render thread; read by the main thread for display only.
        // Plain float is sufficient: a one-frame stale read is acceptable for a counter.
        float                             SmoothedDeltaTime               = 1.f / 60.f;

        // Render thread lifecycle — started in Run(), joined in Deinitialize().
        std::thread                       RenderThread                    = {};
        PaddedAtomic<bool>                RequestTerminate                = {}; // signals both loops to exit
        PaddedAtomic<bool>                CloseRequested                  = {}; // MainThreadRun's own exit condition
        bool                              PipelineCachePersistenceEnabled = false;
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

        /// @brief Requests a clean shutdown — same effect as the OS window-close path.
        static void             RequestClose();

        static void             MainThreadRun();
        static void             RenderThreadRun();

    private:
        static void OnWatchedFileChanged(void* context, const Core::VFS::VFSPath& path, Core::VFS::WatchEventKind kind);

        Engine()              = delete;
        Engine(const Engine&) = delete;
        ~Engine()             = delete;
    };
    ZDEFINE_PTR(Engine);
} // namespace ZEngine
