#include <Applications/AppRenderPipeline.h>
#include <Applications/GameApplication.h>
#include <Engine.h>
#include <Helpers/ThreadPool.h>
#include <Logging/LoggerDefinition.h>
#include <Managers/AssetManager.h>
#include <Windows/GameWindow.h>

#ifdef __APPLE__
#include <pthread/pthread.h>
#endif

namespace ZEngine
{
    static const uint8_t                      k_mailbox_buffer_size = 4;
    static std::atomic_bool                   s_request_terminate   = false;
    static std::mutex                         g_mailbox_mut;
    static std::shared_mutex                  g_mutex                                   = {};
    static EngineContextPtr                   g_engine_ctx                              = nullptr;
    static Applications::GameApplicationPtr   g_app                                     = nullptr;
    static Applications::AppRenderPipelinePtr g_appRenderPipeline                       = nullptr;
    static std::thread                        g_render_thread                           = {};
    static Applications::RenderPayload        g_mailbox_payloads[k_mailbox_buffer_size] = {};
    static std::atomic_int                    g_mailbox_ready_idx                       = -1;
    static std::condition_variable            g_mailbox_ready_cv                        = {};

    static int                                g_write_idx                               = 0;
    static int                                g_pending_idx                             = -1;

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
            g_mailbox_payloads[i].UIOverlay.IndexedCmds.resize(20);
            g_mailbox_payloads[i].UIOverlay.ScissorCmds.resize(20);
            g_mailbox_payloads[i].UIOverlay.TextureIds.resize(20);
        }
        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        std::unique_lock l(g_mutex);

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
        s_request_terminate = false;
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
        static int writable_payload_idx = 0;

        while (auto window = g_engine_ctx->Window)
        {
            if (s_request_terminate.load(std::memory_order_acquire))
            {
                break;
            }

            if (g_engine_ctx->Device->SwapchainResizeRequested)
            {
                {
                    std::unique_lock l(g_engine_ctx->Device->SwapchainMutex);
                    g_engine_ctx->Device->ResizeSwapchain();
                    g_engine_ctx->Device->SwapchainResizeHandled   = true;
                    g_engine_ctx->Device->SwapchainResizeRequested = false;
                }
                g_engine_ctx->Device->SwapchainCond.notify_all();
            }

            float dt = window->GetDeltaTime();

            window->PollEvent();

            if (window->IsMinimized())
            {
                continue;
            }

            g_app->Update(dt);

            {
                std::unique_lock l(g_mailbox_mut);
                g_mailbox_ready_cv.wait(l, [&] { return g_pending_idx == -1; });

                int   next_payload_idx            = g_write_idx;

                auto& r_payload                   = g_mailbox_payloads[next_payload_idx];
                r_payload.UIOverlay.DrawDataIndex = 0;

                if (g_app->EnableRenderOverlay)
                {
                    g_app->RenderPipeline->BeginOverlayFrame();
                    g_app->OnRenderUI();
                    g_app->RenderPipeline->EndOverlayFrame();

                    r_payload.RenderUIOverlay = true;
                    g_appRenderPipeline->FillOverlayPayload(r_payload.UIOverlay);
                }

                g_app->PrepareScene(r_payload);

                g_pending_idx = next_payload_idx;
                g_write_idx   = (g_write_idx + 1) % k_mailbox_buffer_size;
            }
            g_mailbox_ready_cv.notify_one();
        }
    }

    void Engine::RenderThreadRun()
    {
#ifdef __APPLE__
        pthread_setname_np("RenderThread");
#endif
        while (true)
        {
            int idx = -1;
            {
                std::unique_lock l(g_mailbox_mut);
                g_mailbox_ready_cv.wait(l, [&] { return g_pending_idx != -1; });

                if (s_request_terminate.load(std::memory_order_acquire))
                {
                    break;
                }

                idx           = g_pending_idx;
                g_pending_idx = -1;
            }

            ZENGINE_VALIDATE_ASSERT(idx > -1, "Invalid payload index")

            Applications::RenderPayload& r_payload = g_mailbox_payloads[idx];

            auto                         pipeline  = g_app->RenderPipeline;

            if (r_payload.ResizeRenderTarget)
            {
                pipeline->ResizeRenderTarget(r_payload.RenderTargetW, r_payload.RenderTargetH);
            }

            pipeline->BeginFrame();
            pipeline->RenderScene(r_payload.Camera, r_payload.Scene);
            if (r_payload.RenderUIOverlay)
            {
                pipeline->RenderOverlay(r_payload.UIOverlay);
            }
            pipeline->EndFrame();

            g_mailbox_ready_cv.notify_one();
        }
    }

    void Engine::Run()
    {
        Managers::AssetManager::Run();
        g_render_thread = std::thread(Engine::RenderThreadRun);
        MainThreadRun();

        if (s_request_terminate.load(std::memory_order_acquire))
        {
            g_mailbox_ready_cv.notify_all();
            Deinitialize();
        }
    }
} // namespace ZEngine
