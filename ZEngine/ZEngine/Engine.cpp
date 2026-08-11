#include <GLFW/glfw3.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Windows/GameWindow.h>
#include <chrono>

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

        g_engine_ctx = ZPushStruct(&arena, EngineContext);

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

        memory->CreateBudgetedArena(memory->Budget.AssetManager, &g_engine_ctx->AssetArena);
        Managers::AssetManager::Initialize(&g_engine_ctx->AssetArena, g_engine_ctx->Device, app->WorkingSpacePath);

        memory->CreateBudgetedArena(memory->Budget.Input, &g_engine_ctx->InputArena);
        g_engine_ctx->InputManager = ZPushStructCtor(&arena, Input::InputManager);
        g_engine_ctx->InputManager->Initialize(&g_engine_ctx->InputArena);

        glfwSetScrollCallback(static_cast<GLFWwindow*>(window->GetNativeWindow()), [](GLFWwindow*, double, double yoffset) {
            if (g_engine_ctx && g_engine_ctx->InputManager)
                g_engine_ctx->InputManager->AccumulateScroll(yoffset);
        });

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

        // Step 3 — drain async import queue before pipeline/device teardown
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
        while (!s_close_requested.load(std::memory_order_acquire))
        {
            if (!g_engine_ctx || !g_engine_ctx->Window || !g_engine_ctx->Device)
            {
                break;
            }

            auto  window = g_engine_ctx->Window;

            float dt     = window->GetDeltaTime();

            window->PollEvent();

            if (window->IsMinimized())
            {
                continue;
            }

            g_app->Update(dt);

            auto     pipeline = g_app->RenderPipeline;

            uint32_t head     = pipeline->MailBoxBufferHead.value.load(std::memory_order_acquire);
            uint32_t next     = (head + 1) % pipeline->MaxMailBoxBufferCount;
            uint32_t tail     = pipeline->MailBoxBufferTail.value.load(std::memory_order_acquire);

            // Buffer full, drop frame (non-blocking)
            if (next == tail)
            {
                continue;
            }

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
        }
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

            pipeline->BeginFrame();
            pipeline->RenderScene(r_payload.Camera, r_payload.Scene);
            if (r_payload.RenderUIOverlay.value.load(std::memory_order_acquire))
            {
                pipeline->RenderOverlay(r_payload.UIOverlay);
            }
            pipeline->EndFrame();

            uint32_t next = (tail + 1) % pipeline->MaxMailBoxBufferCount;

            pipeline->MailBoxBufferTail.value.store(next, std::memory_order_release);
        }
    }

    void Engine::Run()
    {
        Managers::AssetManager::Run();
        g_render_thread = std::thread(Engine::RenderThreadRun);
        MainThreadRun();

        // OnClosing fires while all subsystems are live; Deinitialize sets s_request_terminate
        g_app->OnClosing();

        Deinitialize();
    }
} // namespace ZEngine
