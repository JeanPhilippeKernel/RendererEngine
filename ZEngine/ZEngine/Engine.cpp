#include <pch.h>
#include <Engine.h>
#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine
{
    static bool                                                  s_request_terminate{false};
    static std::shared_mutex                                     g_mutex;
    static Helpers::WeakRef<Windows::CoreWindow>                 g_current_window = nullptr;
    static Helpers::Scope<Rendering::Renderers::GraphicRenderer> g_renderer       = Helpers::CreateScope<Rendering::Renderers::GraphicRenderer>();
    static Helpers::Scope<Hardwares::VulkanDevice>               g_device         = Helpers::CreateScope<Hardwares::VulkanDevice>();

    void                                                         Engine::Initialize(const EngineConfiguration& engine_configuration, const Helpers::Ref<ZEngine::Windows::CoreWindow>& window)
    {
        g_current_window = window;
        Logging::Logger::Initialize(engine_configuration.LoggerConfiguration);

        window->Initialize();
        g_device->Initialize(g_current_window);
        g_renderer->Initialize(g_device.get());

        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        std::unique_lock l(g_mutex);
        if (auto window = g_current_window.lock())
        {
            window->Deinitialize();
        }
        g_renderer->Deinitialize();
        g_renderer.reset();

        g_device->Deinitialize();
    }

    void Engine::Dispose()
    {
        s_request_terminate = false;

        Logging::Logger::Dispose();
        g_device->Dispose();
        g_device.reset();
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

            if (g_renderer->EnqueuedResizeRequests.Size())
            {
                Rendering::Renderers::ResizeRequest req;
                if (g_renderer->EnqueuedResizeRequests.Pop(req))
                {
                    g_renderer->RenderGraph->Resize(req.Width, req.Height);
                    continue;
                }
            }

            /*On Render*/
            g_device->NewFrame();
            g_renderer->ImguiRenderer->NewFrame();
            auto buffer = g_device->GetCommandBuffer();
            {

                window->Render(g_renderer.get(), buffer);

                g_renderer->ImguiRenderer->DrawFrame(g_device->CurrentFrameIndex, buffer);
            }
            g_device->EnqueueCommandBuffer(buffer);
            g_device->Present();
        }

        if (s_request_terminate)
        {
            Deinitialize();
        }
    }

    Helpers::Ref<Windows::CoreWindow> Engine::GetWindow()
    {
        std::shared_lock l(g_mutex);
        return g_current_window.lock();
    }
} // namespace ZEngine
