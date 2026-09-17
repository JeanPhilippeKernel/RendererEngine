#include <GLFW/glfw3.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/MainThreadScheduler.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/ECS/Reflection/BuiltInComponentReflection.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
#include <ZEngine/ECS/Systems/TransformSyncSystem.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Engine/FixedTimestepAccumulator.h>
#include <ZEngine/Engine/FrameRateCap.h>
#include <ZEngine/Engine/FrameTimer.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/EnvironmentLighting.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Windows/GameWindow.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

#ifdef __APPLE__
#include <mach/mach.h>
#include <pthread/pthread.h>
#endif

using namespace std::chrono_literals;

namespace ZEngine
{
    static EngineContextPtr g_engine_ctx = nullptr;

    // Read memory.geometry_streaming_mb from a project.json file.
    // Returns 0 if the file is absent, unparseable, or the key is missing — callers
    // treat 0 as "use auto-detection from device VRAM".
    static VkDeviceSize     ReadGeometryBudgetOverride(cstring config_file)
    {
        if (!config_file || config_file[0] == '\0')
            return 0;
        std::ifstream f(config_file);
        if (!f.is_open())
            return 0;
        auto json = nlohmann::json::parse(f, nullptr, /*exceptions=*/false);
        if (json.is_discarded() || !json.contains("memory"))
            return 0;
        const auto& mem = json["memory"];
        if (!mem.contains("geometry_streaming_mb"))
            return 0;
        return static_cast<VkDeviceSize>(mem["geometry_streaming_mb"].get<uint32_t>()) << 20;
    }

    // Read rendering.environment_lighting_quality from a project.json file.
    // Missing, malformed, or unrecognized values retain the documented Standard tier.
    static Rendering::EnvironmentLightingBakeSettings ReadEnvironmentLightingQuality(cstring config_file)
    {
        const auto fallback = Rendering::ResolveEnvironmentLightingQuality(Rendering::EnvironmentLightingQualityTier::Standard);
        if (!config_file || config_file[0] == '\0')
            return fallback;

        std::ifstream file(config_file);
        if (!file.is_open())
            return fallback;

        const auto json = nlohmann::json::parse(file, nullptr, /*exceptions=*/false);
        if (json.is_discarded() || !json.contains("rendering") || !json["rendering"].is_object())
            return fallback;

        const auto& rendering = json["rendering"];
        if (!rendering.contains("environment_lighting_quality") || !rendering["environment_lighting_quality"].is_string())
            return fallback;

        const std::string quality = rendering["environment_lighting_quality"].get<std::string>();
        if (quality == "low")
            return Rendering::ResolveEnvironmentLightingQuality(Rendering::EnvironmentLightingQualityTier::Low);
        if (quality == "high")
            return Rendering::ResolveEnvironmentLightingQuality(Rendering::EnvironmentLightingQualityTier::High);
        return fallback;
    }

    // Read rendering.environment_lighting_budget_mb from project.json.
    // Missing, malformed, zero, and overflowing values retain the documented default.
    static VkDeviceSize ReadEnvironmentLightingMemoryBudget(cstring config_file)
    {
        if (!config_file || config_file[0] == '\0')
            return Rendering::DefaultEnvironmentLightingMemoryBudget;

        std::ifstream file(config_file);
        if (!file.is_open())
            return Rendering::DefaultEnvironmentLightingMemoryBudget;

        const auto json = nlohmann::json::parse(file, nullptr, /*exceptions=*/false);
        if (json.is_discarded() || !json.contains("rendering") || !json["rendering"].is_object())
            return Rendering::DefaultEnvironmentLightingMemoryBudget;

        const auto& rendering = json["rendering"];
        if (!rendering.contains("environment_lighting_budget_mb") || !rendering["environment_lighting_budget_mb"].is_number_unsigned())
            return Rendering::DefaultEnvironmentLightingMemoryBudget;

        constexpr uint64_t bytes_per_megabyte = 1024ULL * 1024ULL;
        const uint64_t     megabytes          = rendering["environment_lighting_budget_mb"].get<uint64_t>();
        if (megabytes == 0 || megabytes > std::numeric_limits<uint64_t>::max() / bytes_per_megabyte)
            return Rendering::DefaultEnvironmentLightingMemoryBudget;
        return megabytes * bytes_per_megabyte;
    }

    void Engine::Initialize(Core::Memory::MemoryManager* memory, Windows::WindowConfigurationPtr window_cfg_ptr, Applications::GameApplicationPtr app)
    {
        ZENGINE_VALIDATE_ASSERT(memory != nullptr, "Engine::Initialize: memory is null — Obelisk must call MemoryManager::Initialize first")
        ZENGINE_VALIDATE_ASSERT(Logging::Logger::IsInitialized(), "Engine::Initialize: Logger not initialized — Obelisk must call Logger::Initialize first")
        ZENGINE_VALIDATE_ASSERT(Helpers::ThreadPoolHelper::IsInitialized(), "Engine::Initialize: ThreadPool not initialized — Obelisk must call ThreadPoolHelper::Initialize first")

        auto& arena  = memory->MainArena;

        g_engine_ctx = ZPushStructCtor(&arena, EngineContext);

        auto window  = ZPushStructCtor(&arena, Windows::GameWindow);
        window->SetCallbackFunction(std::bind(&Applications::GameApplication::ProcessEvent, app, std::placeholders::_1));
        window->Initialize(&arena, *window_cfg_ptr);
        g_engine_ctx->Window = window;

        // Device-owned CPU state (command buffers, shaders, render graph, and
        // swapchain state) must consume the declared VulkanDevice budget rather
        // than silently taking unbounded capacity from MainArena.
        memory->CreateBudgetedArena(memory->Budget.VulkanDevice, &g_engine_ctx->VulkanDeviceArena);
        g_engine_ctx->Device         = ZPushStructCtor(&g_engine_ctx->VulkanDeviceArena, Hardwares::VulkanDevice);
        uint32_t worker_thread_count = std::max(1u, (uint32_t) (Helpers::ThreadPoolHelper::Pool->MaxThreadCount / 2u));
        g_engine_ctx->Device->Initialize(&g_engine_ctx->VulkanDeviceArena, window, worker_thread_count);

        memory->CreateBudgetedArena(memory->Budget.VirtualFS, &g_engine_ctx->VFSArena);
        auto vfs_ctx = ZPushStructCtor(&g_engine_ctx->VFSArena, Core::VFS::VFSContext);
        vfs_ctx->Initialize(&g_engine_ctx->VFSArena);
        g_engine_ctx->VFS = vfs_ctx;

        {
            const std::string engine_dir = std::filesystem::current_path().string() + "/ZodiacEngine";
            g_engine_ctx->EngineAssetsBackend.Initialize(engine_dir.c_str(), Core::VFS::VFSBackendCaps::Read | Core::VFS::VFSBackendCaps::Write | Core::VFS::VFSBackendCaps::List, &g_engine_ctx->VFSArena);
            auto mount_path = Core::VFS::VFSPath::Parse("/ZodiacEngine");
            if (mount_path.Succeeded())
            {
                auto res = vfs_ctx->Mount(&g_engine_ctx->EngineAssetsBackend, mount_path.Value(), -1);
                (void) res;
            }

            const bool has_writable_workspace = app->WorkingSpacePath && app->WorkingSpacePath[0] != '\0' && app->VFSBackend && Core::VFS::HasCap(app->VFSBackend->Capabilities(), Core::VFS::VFSBackendCaps::Write);
            const auto cache_mount_path       = Core::VFS::VFSPath::Parse("/cache/pso");
            if (has_writable_workspace && cache_mount_path.Succeeded())
            {
                const std::filesystem::path cache_directory  = std::filesystem::path(app->WorkingSpacePath) / ".zodiacengine" / "cache" / "pso";
                const std::string           native_cache_dir = cache_directory.string();
                g_engine_ctx->PipelineCacheBackend.Initialize(native_cache_dir.c_str(), Core::VFS::VFSBackendCaps::Read | Core::VFS::VFSBackendCaps::Write | Core::VFS::VFSBackendCaps::List, &g_engine_ctx->VFSArena);
                if (vfs_ctx->Mount(&g_engine_ctx->PipelineCacheBackend, cache_mount_path.Value(), 1).Succeeded() && vfs_ctx->CreateDir(cache_mount_path.Value()).Succeeded())
                    g_engine_ctx->PipelineCachePersistenceEnabled = true;
            }

            if (!g_engine_ctx->PipelineCachePersistenceEnabled)
                ZENGINE_CORE_WARN("PSO disk cache is disabled because no writable project VFS cache mount is configured")

            if (g_engine_ctx->PipelineCachePersistenceEnabled && g_engine_ctx->Device->PipelineStateCache)
            {
                g_engine_ctx->Device->PipelineStateCache->LoadDriverPipelineCache(*vfs_ctx);
                g_engine_ctx->Device->PipelineStateCache->LoadWarmupRecipes(*vfs_ctx);
            }
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

        ECS::ComponentReflectionRegistry::Get().Initialize(&g_engine_ctx->ECSArena);
        ECS::Components::RegisterBuiltInComponentReflection();

        // ImportPipeline owns importer sub-arenas and the bounded CPU decode slabs used
        // by RenderResourceManager. The 4 GiB profile capacity covers their simultaneous
        // reservation at the maximum 16-worker configuration.
        memory->CreateBudgetedArena(memory->Budget.ImportPipeline, &g_engine_ctx->ImportPipelineArena);
        memory->CreateBudgetedArena(memory->Budget.UIContext, &g_engine_ctx->UIContextArena);
        g_engine_ctx->ImportCoordinator = ZPushStructCtor(&g_engine_ctx->AssetArena, Importers::ImportCoordinator);
        g_engine_ctx->ImportCoordinator->Initialize(&g_engine_ctx->AssetArena, g_engine_ctx->VFS, Managers::AssetManager::Instance()->Registry);

        g_engine_ctx->GltfImporter.Initialize(&g_engine_ctx->ImportPipelineArena);
        g_engine_ctx->FbxImporter.Initialize(&g_engine_ctx->ImportPipelineArena);
        g_engine_ctx->AssimpImporter.Initialize(&g_engine_ctx->ImportPipelineArena);
        g_engine_ctx->EnvironmentMapImporter.Initialize(&g_engine_ctx->ImportPipelineArena);
        g_engine_ctx->TextureImporter.Initialize(&g_engine_ctx->ImportPipelineArena);
        g_engine_ctx->ImportCoordinator->RegisterImporter(&g_engine_ctx->GltfImporter);
        g_engine_ctx->ImportCoordinator->RegisterImporter(&g_engine_ctx->FbxImporter);
        g_engine_ctx->ImportCoordinator->RegisterImporter(&g_engine_ctx->AssimpImporter);
        g_engine_ctx->ImportCoordinator->RegisterImporter(&g_engine_ctx->EnvironmentMapImporter);
        g_engine_ctx->ImportCoordinator->RegisterImporter(&g_engine_ctx->TextureImporter);

        // Geometry streaming budget — auto-detected from device VRAM, overridable via
        // project.json "memory.geometry_streaming_mb". Must be set before RRM::Initialize
        // since InitGlobalBuffers reads it during VkBuffer allocation.
        g_engine_ctx->Device->GeometryStreamingBudget         = ReadGeometryBudgetOverride(app->ConfigFile);
        g_engine_ctx->Device->EnvironmentLightingBakeSettings = ReadEnvironmentLightingQuality(app->ConfigFile);
        g_engine_ctx->Device->EnvironmentLightingMemoryBudget = ReadEnvironmentLightingMemoryBudget(app->ConfigFile);

        // RenderResourceManager — GPU lifetime authority, bridges asset layer and VulkanDevice
        g_engine_ctx->RenderResourceManager                   = ZPushStructCtor(&g_engine_ctx->AssetArena, Rendering::RenderResourceManager);
        g_engine_ctx->RenderResourceManager->Initialize(g_engine_ctx->Device, Managers::AssetManager::Instance()->Registry, &g_engine_ctx->ImportPipelineArena);
        g_engine_ctx->Device->RRM = g_engine_ctx->RenderResourceManager;

        // Now that RRM is live, create the hot-pink fallback texture for missing assets
        Managers::AssetManager::InitFallbackTexture();

        // Wire FileWatcher: Modified → AssetRegistry + ImportCoordinator::Enqueue(Immediate)
        if (app->WorkingSpacePath && app->WorkingSpacePath[0] != '\0')
        {
            g_engine_ctx->VFSDirectoryCache.Initialize(&g_engine_ctx->AssetArena);
            g_engine_ctx->VFSScanner.Initialize(&g_engine_ctx->AssetArena);
            g_engine_ctx->VFSScanner.SetAssetRegistry(Managers::AssetManager::Instance()->Registry);

            static_cast<Core::VFS::VFSContext*>(g_engine_ctx->VFS)->InitWatcher(app->WorkingSpacePath, &g_engine_ctx->VFSDirectoryCache, &g_engine_ctx->VFSScanner, Managers::AssetManager::Instance()->Registry, g_engine_ctx->ImportCoordinator, &Engine::OnWatchedFileChanged, g_engine_ctx);
        }

        glfwSetScrollCallback(static_cast<GLFWwindow*>(window->GetNativeWindow()), [](GLFWwindow*, double, double yoffset) {
            if (g_engine_ctx && g_engine_ctx->InputManager)
                g_engine_ctx->InputManager->AccumulateScroll(yoffset);
        });

        Core::MainThreadScheduler::Initialize(&arena);

        app->CurrentWindow = g_engine_ctx->Window;
        g_engine_ctx->App  = app;

        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        // Step 1 — signal all loops to exit
        g_engine_ctx->RequestTerminate.value.store(true, std::memory_order_release);

        // Step 2 — join render thread before any GPU resource is destroyed
        if (g_engine_ctx->RenderThread.joinable())
        {
            g_engine_ctx->RenderThread.join();
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
        g_engine_ctx->App->RenderPipeline->Shutdown();

        // Step 12 — close VFS file handles
        if (g_engine_ctx->VFS)
        {
            if (g_engine_ctx->PipelineCachePersistenceEnabled && g_engine_ctx->Device && g_engine_ctx->Device->PipelineStateCache)
            {
                g_engine_ctx->Device->PipelineStateCache->SaveWarmupRecipes(*g_engine_ctx->VFS);
                g_engine_ctx->Device->PipelineStateCache->SaveDriverPipelineCache(*g_engine_ctx->VFS);
                g_engine_ctx->Device->PipelineStateCache->LogTelemetry();
            }
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
        RequestClose();
        return true;
    }

    void Engine::RequestClose()
    {
        g_engine_ctx->CloseRequested.value.store(true, std::memory_order_release);
    }

    void Engine::MainThreadRun()
    {
        Timing::FrameTimer               frame_timer;
        Timing::FixedTimestepAccumulator accumulator;
        Timing::FrameRateCap             frame_cap;

        uint64_t                         frame_index = 0;

        ZENGINE_CORE_INFO("Engine main loop starting — FixedDT={:.4f}s MaxSteps=5", accumulator.FixedDt())

        while (!g_engine_ctx->CloseRequested.value.load(std::memory_order_acquire))
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
            g_engine_ctx->App->Update(raw_dt);

            auto pipeline = g_engine_ctx->App->RenderPipeline;

            // UI is presentation-paced and double-buffered independently from
            // camera state. It may retain its previous frame without delaying
            // input, simulation, or the state read by the render thread.
            if (g_engine_ctx->App->EnableRenderOverlay)
            {
                uint32_t                      overlay_slot = 0;
                Applications::OverlayPayload* overlay      = nullptr;
                if (pipeline->BeginOverlayWrite(overlay, overlay_slot))
                {
                    pipeline->BeginOverlayFrame(raw_dt);
                    g_engine_ctx->App->OnRenderUI();
                    pipeline->EndOverlayFrame();
                    pipeline->FillOverlayPayload(*overlay, overlay_slot);
                    pipeline->PublishOverlay(overlay_slot);
                }
            }

            if (g_engine_ctx->Scene && g_engine_ctx->App->CurrentScene)
            {
                ECS::Systems::SyncHierarchy(*g_engine_ctx->Scene);
                ECS::Systems::SyncECSToRenderScene(*g_engine_ctx->Scene, alpha, *g_engine_ctx->App->CurrentScene);
                ECS::Systems::SyncECSToLights(*g_engine_ctx->Scene, *g_engine_ctx->App->CurrentScene);
            }

            Applications::RenderFrameState state = {};
            g_engine_ctx->App->PrepareScene(state);
            if (state.Scene)
            {
                // Scene mutation happens on this thread. Send an immutable
                // configuration copy with its revision through the bounded
                // frame-state mailbox instead of exposing Scene->Sky to the
                // render thread.
                state.Sky            = state.Scene->Sky;
                state.CelestialLight = state.Scene->CelestialLight;
                state.SkyRevision    = state.Scene->SkyRevision.value.load(std::memory_order_acquire);
            }
            state.RenderOverlay = g_engine_ctx->App->EnableRenderOverlay;
            pipeline->PublishFrameState(state);

            //  Frame rate cap — applied unconditionally.
            //  Vsync throttles the GPU present in the render thread; the main loop
            //  still needs a cap so physics/animation receive a sensible raw_dt
            //  and the CPU is not burned spinning the mailbox at 100k+ Hz.
            frame_cap.WaitForFrameBudget();
        }

        ZENGINE_CORE_INFO("Engine main loop exited after {} frames", frame_index)
    }

    void Engine::RenderThreadRun()
    {
        // Measures wall-clock time between consecutive EndFrame() calls, which
        // includes the vsync wait. This is the true GPU presentation rate.
        Timing::FrameTimer render_timer;

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
        uint32_t                       retained_overlay_slot   = Applications::AppRenderPipeline::MaxOverlayBufferCount;
        Applications::OverlayPayload*  retained_overlay        = nullptr;
        Applications::RenderFrameState retained_frame_state    = {};
        bool                           has_frame_state         = false;
        uint64_t                       applied_resize_sequence = 0;

        while (true)
        {
            if (g_engine_ctx->RequestTerminate.value.load(std::memory_order_acquire))
            {
                break;
            }

            auto pipeline = g_engine_ctx->App->RenderPipeline;

            // Once the GPU device is lost (see VulkanDevice::CheckDeviceLost), continuing to
            // call into Vulkan is undefined behaviour and has been observed to segfault inside
            // the loader rather than return an error. Freeze the render loop instead of
            // crashing — there is no recovery path yet, so this is a safe stop, not a resume.
            if (pipeline->Device && pipeline->Device->IsDeviceLost.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            Applications::RenderFrameState next_frame_state = {};
            if (pipeline->TryReadFrameState(next_frame_state))
            {
                retained_frame_state = next_frame_state;
                has_frame_state      = true;
            }
            if (!has_frame_state)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }

            uint32_t                              next_overlay_slot = 0;
            Applications::OverlayPayload*         next_overlay      = nullptr;
            const bool                            has_next_overlay  = pipeline->BeginOverlayRead(next_overlay, next_overlay_slot);
            const Applications::RenderFrameState& state             = retained_frame_state;

            if (state.ResizeSequence != applied_resize_sequence && state.RenderTargetW > 0 && state.RenderTargetH > 0)
            {
                pipeline->ResizeRenderTarget(state.RenderTargetW, state.RenderTargetH);
                applied_resize_sequence = state.ResizeSequence;
            }

            const bool frame_valid = pipeline->BeginFrame();
            if (frame_valid && state.Scene)
            {
                const auto* overlay = state.RenderOverlay ? (has_next_overlay ? &next_overlay->ZUIOverlay : (retained_overlay ? &retained_overlay->ZUIOverlay : nullptr)) : nullptr;
                pipeline->RenderScene(state.Camera, state.Scene, state.Sky, state.CelestialLight, state.SkyRevision, overlay);
            }
            pipeline->EndFrame();

            // Update SmoothedDeltaTime with the render thread's smoothed frame time.
            // End() is called after EndFrame() so the vsync wait is included in the sample.
            render_timer.End();
            if (g_engine_ctx)
                g_engine_ctx->SmoothedDeltaTime = render_timer.SmoothedDelta();

            if (has_next_overlay)
            {
                if (retained_overlay)
                    pipeline->EndOverlayRead(retained_overlay_slot);
                retained_overlay_slot = next_overlay_slot;
                retained_overlay      = next_overlay;
            }
            pipeline->NotifyOverlayFrameComplete();

            const uint32_t buffered_frame_count = pipeline->Device->SwapchainPtr->BufferredFrameCount;
            pipeline->CurrentFrameContextIndex  = buffered_frame_count > 0 ? (pipeline->CurrentFrameContextIndex + 1) % buffered_frame_count : 0;
        }

        if (retained_overlay)
            g_engine_ctx->App->RenderPipeline->EndOverlayRead(retained_overlay_slot);
    }

    void Engine::OnWatchedFileChanged(void* context, const Core::VFS::VFSPath& path, Core::VFS::WatchEventKind kind)
    {
        if (kind != Core::VFS::WatchEventKind::Modified)
            return;

        const Core::VFS::VFSPathComponent extension = path.Extension();
        if (!extension.Data || extension.Length != 4 || extension.Data[0] != '.' || extension.Data[1] != 's' || extension.Data[2] != 'p' || extension.Data[3] != 'v')
            return;

        EngineContext* engine_context = static_cast<EngineContext*>(context);
        if (engine_context && engine_context->Device)
            engine_context->Device->RequestShaderReload(path.CStr());
    }

    void Engine::Run()
    {

        g_engine_ctx->RenderThread = std::thread(Engine::RenderThreadRun);
        MainThreadRun();

        // OnClosing fires while all subsystems are live; Deinitialize sets RequestTerminate
        g_engine_ctx->App->OnClosing();

        Deinitialize();
    }
} // namespace ZEngine
