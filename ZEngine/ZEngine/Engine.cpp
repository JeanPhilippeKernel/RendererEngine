#include <GLFW/glfw3.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Engine/FixedTimestepAccumulator.h>
#include <ZEngine/Engine/FrameRateCap.h>
#include <ZEngine/Engine/FrameTimer.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/AssimpImporter.h>
#include <ZEngine/Importers/EnvironmentMapImporter.h>
#include <ZEngine/Importers/GltfImporter.h>
#include <ZEngine/Importers/ImportCoordinator.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Windows/GameWindow.h>
#include <chrono>
#include <filesystem>

#ifdef __APPLE__
#include <mach/mach.h>
#include <pthread/pthread.h>
#endif

using namespace std::chrono_literals;

namespace ZEngine
{
    static std::atomic_bool                 s_request_terminate = false;
    static std::atomic_bool                 s_close_requested   = false;
    static EngineContextPtr                 g_engine_ctx        = nullptr;
    static Applications::GameApplicationPtr g_app               = nullptr;
    static std::thread                      g_render_thread     = {};

    void                                    Engine::Initialize(Core::Memory::MemoryManager* memory, Windows::WindowConfigurationPtr window_cfg_ptr, Applications::GameApplicationPtr app)
    {
        ZENGINE_VALIDATE_ASSERT(memory != nullptr, "Engine::Initialize: memory is null — Obelisk must call MemoryManager::Initialize first")
        ZENGINE_VALIDATE_ASSERT(Logging::Logger::IsInitialized(), "Engine::Initialize: Logger not initialized — Obelisk must call Logger::Initialize first")
        ZENGINE_VALIDATE_ASSERT(Helpers::ThreadPoolHelper::IsInitialized(), "Engine::Initialize: ThreadPool not initialized — Obelisk must call ThreadPoolHelper::Initialize first")

        auto& arena  = memory->MainArena;

        g_engine_ctx = ZPushStructCtor(&arena, EngineContext);

        auto window  = ZPushStructCtor(&arena, Windows::GameWindow);
        window->SetCallbackFunction(std::bind(&Applications::GameApplication::ProcessEvent, app, std::placeholders::_1));
        window->Initialize(&arena, *window_cfg_ptr);
        g_engine_ctx->Window         = window;

        g_engine_ctx->Device         = ZPushStructCtor(&arena, Hardwares::VulkanDevice);
        uint32_t worker_thread_count = std::max(1u, (uint32_t) (Helpers::ThreadPoolHelper::Pool->MaxThreadCount / 2u));
        g_engine_ctx->Device->Initialize(&arena, window, worker_thread_count);

        memory->CreateBudgetedArena(memory->Budget.VirtualFS, &g_engine_ctx->VFSArena);
        auto vfs_ctx = ZPushStructCtor(&g_engine_ctx->VFSArena, Core::VFS::VFSContext);
        vfs_ctx->Initialize(&g_engine_ctx->VFSArena);
        g_engine_ctx->VFS = vfs_ctx;

        {
            const std::string engine_dir = std::filesystem::current_path().string() + "/ZodiacEngine";
            g_engine_ctx->EngineAssetsBackend.Initialize(engine_dir.c_str(), Core::VFS::VFSBackendCaps::Read | Core::VFS::VFSBackendCaps::Write | Core::VFS::VFSBackendCaps::List, &g_engine_ctx->VFSArena);
            auto mount_path = Core::VFS::VFSPath::Parse("/ZodiacEngine");
            if (mount_path.Succeeded())
                vfs_ctx->Mount(&g_engine_ctx->EngineAssetsBackend, mount_path.Value(), -1);
        }

        memory->CreateBudgetedArena(memory->Budget.AssetManager, &g_engine_ctx->AssetArena);
        Managers::AssetManager::Initialize(&g_engine_ctx->AssetArena, g_engine_ctx->Device, app->WorkingSpacePath);

        memory->CreateBudgetedArena(memory->Budget.Input, &g_engine_ctx->InputArena);
        g_engine_ctx->InputManager = ZPushStructCtor(&arena, Input::InputManager);
        g_engine_ctx->InputManager->Initialize(&g_engine_ctx->InputArena);

        memory->CreateBudgetedArena(memory->Budget.ECSScene, &g_engine_ctx->ECSArena);
        g_engine_ctx->Scene = ZPushStructCtor(&g_engine_ctx->ECSArena, ECS::Scene);
        g_engine_ctx->Scene->Initialize(&g_engine_ctx->ECSArena);
        g_engine_ctx->ActorManager = ZPushStructCtor(&g_engine_ctx->ECSArena, ECS::ActorManager);
        g_engine_ctx->ActorManager->Initialize(&g_engine_ctx->ECSArena, *g_engine_ctx->Scene);
        g_engine_ctx->WorldCommands = ZPushStructCtor(&g_engine_ctx->ECSArena, ECS::WorldCommands);
        g_engine_ctx->WorldCommands->Initialize(&g_engine_ctx->ECSArena);
        g_engine_ctx->WorldTick = ZPushStructCtor(&g_engine_ctx->ECSArena, ECS::WorldTick);
        g_engine_ctx->WorldTick->Initialize(&g_engine_ctx->ECSArena);

        g_engine_ctx->ImportCoordinator = ZPushStructCtor(&g_engine_ctx->AssetArena, Importers::ImportCoordinator);
        g_engine_ctx->ImportCoordinator->Initialize(&g_engine_ctx->AssetArena, g_engine_ctx->VFS, Managers::AssetManager::Instance()->Registry);

        // Register format importers
        // All importers get their own arenas carved from MainArena (3 GB).
        // AssetArena is only 78 MB — not enough for intermediate geometry data.
        static Importers::GltfImporter           s_gltf_importer;
        static Importers::AssimpImporter         s_assimp_importer;
        static Importers::EnvironmentMapImporter s_env_map_importer;
        static Core::Memory::ArenaAllocator      s_gltf_arena;
        static Core::Memory::ArenaAllocator      s_assimp_arena;
        static Core::Memory::ArenaAllocator      s_envmap_arena;
        arena.CreateSubArena(ZMega(64), &s_gltf_arena);
        arena.CreateSubArena(ZMega(350), &s_assimp_arena);
        arena.CreateSubArena(ZMega(32), &s_envmap_arena);
        s_gltf_importer.Initialize(&s_gltf_arena);
        s_assimp_importer.Initialize(&s_assimp_arena);
        s_env_map_importer.Initialize(&s_envmap_arena);
        // GLB/GLTF handled by GltfImporter; FBX/OBJ by AssimpImporter.
        g_engine_ctx->ImportCoordinator->RegisterImporter(&s_gltf_importer);
        g_engine_ctx->ImportCoordinator->RegisterImporter(&s_assimp_importer);
        g_engine_ctx->ImportCoordinator->RegisterImporter(&s_env_map_importer);

        // RenderResourceManager — GPU lifetime authority, bridges asset layer and VulkanDevice
        g_engine_ctx->RenderResourceManager = ZPushStructCtor(&g_engine_ctx->AssetArena, Rendering::RenderResourceManager);
        g_engine_ctx->RenderResourceManager->Initialize(g_engine_ctx->Device, Managers::AssetManager::Instance()->Registry);
        g_engine_ctx->Device->RRM = g_engine_ctx->RenderResourceManager;

        // Now that RRM is live, create the hot-pink fallback texture for missing assets
        Managers::AssetManager::InitFallbackTexture();

        // Wire FileWatcher: Modified → AssetRegistry + ImportCoordinator::Enqueue(Immediate)
        if (app->WorkingSpacePath && app->WorkingSpacePath[0] != '\0')
        {
            static_cast<Core::VFS::VFSContext*>(g_engine_ctx->VFS)->InitWatcher(app->WorkingSpacePath, nullptr, nullptr, Managers::AssetManager::Instance()->Registry, g_engine_ctx->ImportCoordinator);
        }

        glfwSetScrollCallback(static_cast<GLFWwindow*>(window->GetNativeWindow()), [](GLFWwindow*, double, double yoffset) {
            if (g_engine_ctx && g_engine_ctx->InputManager)
                g_engine_ctx->InputManager->AccumulateScroll(yoffset);
        });

        Core::MainThreadScheduler::Initialize(&arena);

        app->CurrentWindow = g_engine_ctx->Window;
        g_app              = app;

        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        // Step 1 — signal all loops to exit
        s_request_terminate.store(true, std::memory_order_release);

        // Step 2 — join render thread before any GPU resource is destroyed
        if (g_render_thread.joinable())
        {
            g_render_thread.join();
        }

        Core::MainThreadScheduler::Shutdown();

        // Step 3 — ECS shutdown: ActorManager before Scene (lifecycle order)
        if (g_engine_ctx->ActorManager)
            g_engine_ctx->ActorManager->Shutdown();
        if (g_engine_ctx->Scene)
            g_engine_ctx->Scene->Shutdown();

        // Step 4 — shut down RRM before pipeline teardown (GPU must still be alive)
        if (g_engine_ctx->RenderResourceManager)
            g_engine_ctx->RenderResourceManager->Shutdown();

        // Step 5 — drain async import queue before pipeline/device teardown
        Managers::AssetManager::Shutdown();

        // Step 4 — destroy framebuffers, render passes, descriptor sets
        g_app->RenderPipeline->Shutdown();

        // Step 12 — close VFS file handles
        if (g_engine_ctx->VFS)
        {
            g_engine_ctx->VFS->Shutdown();
        }

        // Step 13 — destroy logical device, queues, command pools
        g_engine_ctx->Device->Deinitialize();

        // Step 14 — destroy OS window and Vulkan surface (must follow device)
        if (g_engine_ctx->Window)
        {
            g_engine_ctx->Window->Deinitialize();
        }
    }

    void Engine::Dispose()
    {
        g_engine_ctx->Device->Dispose();

        ZENGINE_CORE_INFO("Engine destroyed")
    }

    EngineContextPtr Engine::GetContext()
    {
        return g_engine_ctx;
    }

    bool Engine::OnEngineClosed(Event::EngineClosedEvent& event)
    {
        s_close_requested.store(true, std::memory_order_release);
        return true;
    }

    void Engine::MainThreadRun()
    {
        Timing::FrameTimer               frame_timer;
        Timing::FixedTimestepAccumulator accumulator;
        Timing::FrameRateCap             frame_cap;

        uint64_t                         frame_index = 0;

        ZENGINE_CORE_INFO("Engine main loop starting — FixedDT={:.4f}s MaxSteps=5", accumulator.FixedDt())

        while (!s_close_requested.load(std::memory_order_acquire))
        {
            if (!g_engine_ctx || !g_engine_ctx->Window || !g_engine_ctx->Device)
                break;

            auto window = g_engine_ctx->Window;

            //  Frame bookkeeping
            frame_timer.Begin();
            frame_cap.MarkFrameStart();
            ++frame_index;

            //  Platform events
            window->PollEvent();

            // Pump VFS FileWatcher — drains debounce window, fires Modified/Deleted/Renamed callbacks
            static_cast<Core::VFS::VFSContext*>(g_engine_ctx->VFS)->Tick();

            if (window->IsMinimized())
                continue;

            //  Measure raw delta
            float raw_dt = frame_timer.End();
            accumulator.Accumulate(raw_dt);

            //  Fixed simulation steps
            // ECS systems, Actor OnTick, and WorldCommands flush run inside each
            // fixed step so simulation is frame-rate independent.
            if (g_engine_ctx->WorldTick && g_engine_ctx->WorldTick->SystemCount() > 0)
            {
                while (accumulator.ShouldStep())
                {
                    float fixed_dt = accumulator.FixedDt();

                    g_engine_ctx->WorldTick->Tick(*g_engine_ctx->Scene, fixed_dt, *g_engine_ctx->WorldCommands);
                    g_engine_ctx->WorldCommands->Flush(*g_engine_ctx->Scene);
                    g_engine_ctx->ActorManager->Tick(fixed_dt);
                    g_engine_ctx->Scene->SnapshotTransforms(); // must be after Tick

                    accumulator.ConsumeStep();
                }
            }

            float alpha = accumulator.Alpha();

            // Import coordinator (up to JOBS_PER_TICK jobs per frame)
            if (g_engine_ctx->ImportCoordinator)
                g_engine_ctx->ImportCoordinator->Tick();

            Core::MainThreadScheduler::Drain();

            // Application update (non-ECS game logic)
            g_app->Update(raw_dt);

            // Render payload
            auto     pipeline = g_app->RenderPipeline;
            uint32_t head     = pipeline->MailBoxBufferHead.value.load(std::memory_order_acquire);
            uint32_t next     = (head + 1) % pipeline->MaxMailBoxBufferCount;
            uint32_t tail     = pipeline->MailBoxBufferTail.value.load(std::memory_order_acquire);

            if (next == tail)
                continue; // buffer full — drop frame

            auto& r_payload                   = pipeline->RenderPayloads[head];
            r_payload.UIOverlay.DrawDataIndex = 0;
            r_payload.RenderUIOverlay.value.store(false, std::memory_order_release);

            if (g_app->EnableRenderOverlay)
            {
                pipeline->BeginOverlayFrame();
                g_app->OnRenderUI();
                pipeline->EndOverlayFrame();
                r_payload.RenderUIOverlay.value.store(true, std::memory_order_release);
                pipeline->FillOverlayPayload(r_payload.UIOverlay);
            }

            g_app->PrepareScene(r_payload);

            pipeline->MailBoxBufferHead.value.store(next, std::memory_order_release);

            //  Frame rate cap (vsync-off only)
            if (!window->IsVSyncEnable())
                frame_cap.WaitForFrameBudget();
        }

        ZENGINE_CORE_INFO("Engine main loop exited after {} frames", frame_index)
    }

    void Engine::RenderThreadRun()
    {
#ifdef __APPLE__
        pthread_setname_np("RenderThread");
        thread_port_t                        thread_port = pthread_mach_thread_np(pthread_self());
        thread_time_constraint_policy_data_t policy;
        policy.period      = 50000;
        policy.computation = 20000;
        policy.constraint  = 40000;
        policy.preemptible = 1;

        kern_return_t kr   = thread_policy_set(thread_port, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t) &policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#endif
        while (true)
        {
            if (s_request_terminate.load(std::memory_order_acquire))
            {
                break;
            }

            auto     pipeline = g_app->RenderPipeline;

            uint32_t tail     = pipeline->MailBoxBufferTail.value.load(std::memory_order_acquire);
            uint32_t head     = pipeline->MailBoxBufferHead.value.load(std::memory_order_acquire);

            // Buffer empty
            if (tail == head)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }

            pipeline->CurrentMailBoxBufferHead     = tail;
            Applications::RenderPayload& r_payload = pipeline->RenderPayloads[tail];

            if (r_payload.ResizeRenderTarget.value.load(std::memory_order_acquire))
            {
                pipeline->ResizeRenderTarget(r_payload.RenderTargetW, r_payload.RenderTargetH);
                r_payload.ResizeRenderTarget.value.store(false, std::memory_order_release);
            }

            const bool frame_valid = pipeline->BeginFrame();
            if (frame_valid)
            {
                pipeline->RenderScene(r_payload.Camera, r_payload.Scene);
                if (r_payload.RenderUIOverlay.value.load(std::memory_order_acquire))
                    pipeline->RenderOverlay(r_payload.UIOverlay);
            }
            pipeline->EndFrame();

            uint32_t next = (tail + 1) % pipeline->MaxMailBoxBufferCount;

            pipeline->MailBoxBufferTail.value.store(next, std::memory_order_release);
        }
    }

    void Engine::Run()
    {

        g_render_thread = std::thread(Engine::RenderThreadRun);
        MainThreadRun();

        // OnClosing fires while all subsystems are live; Deinitialize sets s_request_terminate
        g_app->OnClosing();

        Deinitialize();
    }
} // namespace ZEngine
