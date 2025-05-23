#include <pch.h>
#include <Engine.h>
#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine
{
    static bool              s_request_terminate                     = false;
    static std::shared_mutex g_mutex                                 = {};
    static ZRawPtr(Windows::CoreWindow) g_current_window             = nullptr;
    static ZRawPtr(Rendering::Renderers::GraphicRenderer) g_renderer = nullptr;
    static ZRawPtr(Hardwares::VulkanDevice) g_device                 = nullptr;

    void Engine::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZRawPtr(ZEngine::Windows::CoreWindow) const window)
    {
        g_current_window = window;
        g_device         = ZPushStructCtor(arena, Hardwares::VulkanDevice);
        g_renderer       = ZPushStructCtor(arena, Rendering::Renderers::GraphicRenderer);

        g_device->Initialize(arena, window);
        g_renderer->Initialize(g_device);

        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::Deinitialize()
    {
        std::unique_lock l(g_mutex);
        if (g_current_window)
        {
            g_current_window->Deinitialize();
        }
        g_renderer->Deinitialize();

        g_device->Deinitialize();
    }

    void Engine::Dispose()
    {
        s_request_terminate = false;
        g_device->Dispose();

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
        while (g_current_window)
        {
            if (s_request_terminate)
            {
                break;
            }

            float dt = g_current_window->GetDeltaTime();

            g_current_window->PollEvent();

            if (g_current_window->IsMinimized())
            {
                continue;
            }

            /*On Update*/
            g_current_window->Update(dt);

            g_device->Update();
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

                g_current_window->Render(g_renderer, buffer);

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

    Windows::CoreWindow* Engine::GetWindow()
    {
        std::shared_lock l(g_mutex);
        return g_current_window;
    }
} // namespace ZEngine
