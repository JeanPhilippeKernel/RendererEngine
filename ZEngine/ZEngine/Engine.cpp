#include <pch.h>
#include <Engine.h>
#include <Logging/LoggerDefinition.h>

using namespace ZEngine::Hardwares;
using namespace ZEngine::Rendering::Renderers;

namespace ZEngine
{
    static bool                                  s_request_terminate{false};
    static Helpers::WeakRef<Windows::CoreWindow> g_current_window = nullptr;

    void Engine::Initialize(const EngineConfiguration& engine_configuration, const Helpers::Ref<ZEngine::Windows::CoreWindow>& window)
    {
        g_current_window = window;
        Logging::Logger::Initialize(engine_configuration.LoggerConfiguration);

        window->Initialize();

        for (const auto& layer : engine_configuration.WindowConfiguration.RenderingLayerCollection)
        {
            window->PushLayer(layer);
        }

        for (const auto& layer : engine_configuration.WindowConfiguration.OverlayLayerCollection)
        {
            window->PushOverlayLayer(layer);
        }
        window->InitializeLayer();

        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        if (auto window = g_current_window.lock())
        {
            window->Deinitialize();
        }
    }

    void Engine::Dispose()
    {
        s_request_terminate = false;

        Logging::Logger::Dispose();
        ZENGINE_CORE_INFO("Engine destroyed")
    }

    bool Engine::OnEngineClosed(Event::EngineClosedEvent& event)
    {
        s_request_terminate = true;
        return true;
    }

    void Engine::Run()
    {
        s_request_terminate = false;
        while (auto window = g_current_window.lock())
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
            window->Update(dt);

            /*On Render*/
            window->Render();
        }

        if (s_request_terminate)
        {
            Deinitialize();
        }
    }

    Helpers::Ref<Windows::CoreWindow> Engine::GetWindow()
    {
        return g_current_window.lock();
    }
} // namespace ZEngine
