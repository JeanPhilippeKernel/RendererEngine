#include <pch.h>
#include <Engine.h>
#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine
{
    static bool                         s_request_terminate{false};
    static WeakRef<Windows::CoreWindow> g_current_window = nullptr;

    void Engine::Initialize(const EngineConfiguration& engine_configuration, const Ref<ZEngine::Windows::CoreWindow>& window)
    {
        g_current_window = window;
        Logging::Logger::Initialize(engine_configuration.LoggerConfiguration);

        Hardwares::VulkanDevice::Initialize(window);

        window->Initialize();
        GraphicRenderer::SetMainSwapchain(window->GetSwapchain());
        GraphicRenderer::Initialize();
        /*
         * Renderer Post initialization
         */
        ZENGINE_CORE_INFO("Engine initialized")

        for (const auto& layer : engine_configuration.WindowConfiguration.RenderingLayerCollection)
        {
            window->PushLayer(layer);
        }

        for (const auto& layer : engine_configuration.WindowConfiguration.OverlayLayerCollection)
        {
            window->PushOverlayLayer(layer);
        }
        window->InitializeLayer();
    }

    void Engine::Deinitialize()
    {
        if (auto window = g_current_window.lock())
        {
            window->Deinitialize();
        }
        GraphicRenderer::Deinitialize();
        Hardwares::VulkanDevice::Deinitialize();
    }

    void Engine::Dispose()
    {
        s_request_terminate = false;

        Hardwares::VulkanDevice::Dispose();

        ZENGINE_CORE_INFO("Engine destroyed")
        Logging::Logger::Dispose();
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
            GraphicRenderer::Update();
            window->Update(dt);

            /*On Render*/
            window->Render();
        }

        if (s_request_terminate)
        {
            Deinitialize();
        }
    }

    Ref<Windows::CoreWindow> Engine::GetWindow()
    {
        return g_current_window.lock();
    }
} // namespace ZEngine
