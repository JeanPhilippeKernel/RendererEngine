#include <pch.h>
#include <Applications/AppRenderPipeline.h>
#include <Applications/GameApplication.h>
#include <Engine.h>
#include <Logging/LoggerDefinition.h>
#include <Managers/AssetManager.h>
#include <Windows/GameWindow.h>

namespace ZEngine
{
    static bool                               s_request_terminate = false;
    static std::shared_mutex                  g_mutex             = {};
    static EngineContextPtr                   g_engine_ctx        = nullptr;
    static Applications::GameApplicationPtr   g_app               = nullptr;
    static Applications::AppRenderPipelinePtr g_appRenderPipeline = nullptr;

    void                                      Engine::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, Windows::WindowConfigurationPtr window_cfg_ptr, Applications::GameApplicationPtr app)
    {
        g_engine_ctx         = ZPushStruct(arena, EngineContext);
        g_engine_ctx->Device = ZPushStructCtor(arena, Hardwares::VulkanDevice);
        auto window          = ZPushStructCtor(arena, Windows::GameWindow);

        window->Initialize(arena, *window_cfg_ptr);
        window->SetCallbackFunction(std::bind(&Applications::GameApplication::ProcessEvent, app, std::placeholders::_1));
        g_engine_ctx->Window = window;

        g_appRenderPipeline  = ZPushStruct(arena, Applications::AppRenderPipeline);

        g_engine_ctx->Device->Initialize(arena, window);
        g_appRenderPipeline->Initialize(g_engine_ctx->Device);

        Managers::AssetManager::Initialize(arena, g_engine_ctx->Device, app->WorkingSpacePath);

        app->RenderPipeline = g_appRenderPipeline;
        app->CurrentWindow  = g_engine_ctx->Window;
        g_app               = app;

        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        std::unique_lock l(g_mutex);

        if (g_engine_ctx->Window)
        {
            g_engine_ctx->Window->Deinitialize();
        }

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
        s_request_terminate = true;
        return true;
    }

    void Engine::Run()
    {
        Managers::AssetManager::Run();

        s_request_terminate = false;
        while (auto window = g_engine_ctx->Window)
        {
            if (s_request_terminate)
            {
                break;
            }

            float dt = window->GetDeltaTime();

            window->PollEvent();

            if (window->IsMinimized())
            {
                continue;
            }

            /*On Update*/
            g_app->Update(dt);

            /*On Render*/
            g_app->Render();
        }

        if (s_request_terminate)
        {
            Deinitialize();
        }
    }
} // namespace ZEngine
