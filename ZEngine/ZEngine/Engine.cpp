#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/ThreadPool.h>
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
    static std::atomic_bool                   s_request_terminate = false;
    static EngineContextPtr                   g_engine_ctx        = nullptr;
    static Applications::GameApplicationPtr   g_app               = nullptr;
    static Applications::AppRenderPipelinePtr g_appRenderPipeline = nullptr;
    static std::thread                        g_render_thread     = {};

    void                                      Engine::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::WindowConfigurationPtr window_cfg_ptr, Applications::GameApplicationPtr app)
    {
        g_engine_ctx         = ZPushStruct(arena, EngineContext);
        g_engine_ctx->Device = ZPushStructCtor(arena, Hardwares::VulkanDevice);
        auto window          = ZPushStructCtor(arena, Windows::GameWindow);

        window->SetCallbackFunction(std::bind(&Applications::GameApplication::ProcessEvent, app, std::placeholders::_1));
        window->Initialize(arena, *window_cfg_ptr);
        g_engine_ctx->Window         = window;

        g_appRenderPipeline          = ZPushStructCtor(arena, Applications::AppRenderPipeline);

        uint32_t worker_thread_count = std::max(1u, (uint32_t) (Helpers::ThreadPoolHelper::Pool->MaxThreadCount / 2u));
        g_engine_ctx->Device->Initialize(arena, window, worker_thread_count /*, k_mailbox_buffer_size */);
        g_appRenderPipeline->Initialize(g_engine_ctx->Device);

        Managers::AssetManager::Initialize(arena, g_engine_ctx->Device, app->WorkingSpacePath);

        app->RenderPipeline = g_appRenderPipeline;
        app->CurrentWindow  = g_engine_ctx->Window;
        g_app               = app;

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

        Deinitialize();
    }
} // namespace ZEngine
