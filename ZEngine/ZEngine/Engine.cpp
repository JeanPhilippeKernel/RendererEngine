#include <Applications/AppRenderPipeline.h>
#include <Applications/GameApplication.h>
#include <Engine.h>
#include <Helpers/ThreadPool.h>
#include <Logging/LoggerDefinition.h>
#include <Managers/AssetManager.h>
#include <Windows/GameWindow.h>
#include <chrono>
#include <new>

#ifdef __APPLE__
#include <mach/mach.h>
#include <pthread/pthread.h>
#endif

using namespace std::chrono_literals;

namespace ZEngine
{

    struct alignas(std::hardware_destructive_interference_size) PaddedAtomicInt
    {
        std::atomic_uint32_t value;
    };

    static std::atomic_bool                   s_request_terminate = false;
    static EngineContextPtr                   g_engine_ctx        = nullptr;
    static Applications::GameApplicationPtr   g_app               = nullptr;
    static Applications::AppRenderPipelinePtr g_appRenderPipeline = nullptr;
    static std::thread                        g_render_thread     = {};

    static PaddedAtomicInt                    g_head{0};
    static PaddedAtomicInt                    g_tail{0};
    static constexpr uint8_t                  k_mailbox_buffer_size                     = 3;
    static Applications::RenderPayload        g_mailbox_payloads[k_mailbox_buffer_size] = {};

    void                                      Engine::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::WindowConfigurationPtr window_cfg_ptr, Applications::GameApplicationPtr app)
    {
        g_engine_ctx         = ZPushStruct(arena, EngineContext);
        g_engine_ctx->Device = ZPushStructCtor(arena, Hardwares::VulkanDevice);
        auto window          = ZPushStructCtor(arena, Windows::GameWindow);

        window->SetCallbackFunction(std::bind(&Applications::GameApplication::ProcessEvent, app, std::placeholders::_1));
        window->Initialize(arena, *window_cfg_ptr);
        g_engine_ctx->Window = window;

        g_appRenderPipeline  = ZPushStructCtor(arena, Applications::AppRenderPipeline);

        g_engine_ctx->Device->Initialize(arena, window, (Helpers::ThreadPoolHelper::Pool->MaxThreadCount / 2u));
        g_appRenderPipeline->Initialize(g_engine_ctx->Device);

        Managers::AssetManager::Initialize(arena, g_engine_ctx->Device, app->WorkingSpacePath);

        app->RenderPipeline = g_appRenderPipeline;
        app->CurrentWindow  = g_engine_ctx->Window;
        g_app               = app;

        for (size_t i = 0; i < k_mailbox_buffer_size; ++i)
        {
            g_mailbox_payloads[i].UIOverlay.IndexedCmds.resize(100);
            g_mailbox_payloads[i].UIOverlay.ScissorCmds.resize(100);
            g_mailbox_payloads[i].UIOverlay.TextureIds.resize(100);
        }
        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        if (g_engine_ctx->Window)
        {
            g_engine_ctx->Window->Deinitialize();
        }

        g_render_thread.join();
        g_appRenderPipeline->Shutdown();
        g_engine_ctx->Device->Deinitialize();
    }

    void Engine::Dispose()
    {
        s_request_terminate.store(false, std::memory_order_release);
        Managers::AssetManager::Shutdown();
        g_engine_ctx->Device->Dispose();

        ZENGINE_CORE_INFO("Engine destroyed")
    }

    bool Engine::OnEngineClosed(Event::EngineClosedEvent& event)
    {
        s_request_terminate.store(true, std::memory_order_release);
        return true;
    }

    void Engine::MainThreadRun()
    {
        while (!s_request_terminate.load(std::memory_order_acquire))
        {
            if (!g_engine_ctx || !g_engine_ctx->Window || !g_engine_ctx->Device)
            {
                break;
            }

            auto device = g_engine_ctx->Device;

            if (device->SwapchainResizeRequested)
            {
                {
                    std::unique_lock l(g_engine_ctx->Device->SwapchainMutex);
                    device->ResizeSwapchain();
                    device->SwapchainResizeHandled   = true;
                    device->SwapchainResizeRequested = false;
                }
                device->SwapchainCond.notify_all();
            }

            auto  window = g_engine_ctx->Window;

            float dt     = window->GetDeltaTime();

            window->PollEvent();

            if (window->IsMinimized())
            {
                continue;
            }

            g_app->Update(dt);

            uint32_t head = g_head.value.load(std::memory_order_relaxed);

            uint32_t next = (head + 1) % k_mailbox_buffer_size;

            uint32_t tail = g_tail.value.load(std::memory_order_acquire);

            // Buffer full, drop frame (non-blocking)
            if (next == tail)
                continue;
            {
                auto& r_payload                   = g_mailbox_payloads[head];
                r_payload.UIOverlay.DrawDataIndex = 0;
                r_payload.RenderUIOverlay         = false;

                if (g_app->EnableRenderOverlay)
                {
                    g_app->RenderPipeline->BeginOverlayFrame();
                    g_app->OnRenderUI();
                    g_app->RenderPipeline->EndOverlayFrame();

                    r_payload.RenderUIOverlay = true;
                    g_appRenderPipeline->FillOverlayPayload(r_payload.UIOverlay);
                }

                g_app->PrepareScene(r_payload);

                g_head.value.store(next, std::memory_order_release);
            }
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

            uint32_t tail = g_tail.value.load(std::memory_order_relaxed);

            uint32_t head = g_head.value.load(std::memory_order_acquire);

            // Buffer empty
            if (tail == head)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                continue;
            }

            Applications::RenderPayload& r_payload = g_mailbox_payloads[tail];

            auto                         pipeline  = g_app->RenderPipeline;

            if (r_payload.ResizeRenderTarget)
            {
                pipeline->ResizeRenderTarget(r_payload.RenderTargetW, r_payload.RenderTargetH);
                r_payload.ResizeRenderTarget = false;
            }

            pipeline->BeginFrame();
            pipeline->RenderScene(r_payload.Camera, r_payload.Scene);
            if (r_payload.RenderUIOverlay)
            {
                pipeline->RenderOverlay(r_payload.UIOverlay);
            }
            pipeline->EndFrame();

            uint32_t next = (tail + 1) % k_mailbox_buffer_size;

            g_tail.value.store(next, std::memory_order_release);
        }
    }

    void Engine::Run()
    {
        Managers::AssetManager::Run();
        g_render_thread = std::thread(Engine::RenderThreadRun);
        MainThreadRun();

        Deinitialize();
    }
} // namespace ZEngine
