#include <pch.h>
#include <Engine.h>
#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine
{
    static bool              s_request_terminate                                = false;
    static std::shared_mutex g_mutex                                            = {};
    static ZRawPtr(Windows::CoreWindow) g_current_window                        = nullptr;
    static Helpers::Scope<Rendering::Renderers::GraphicRenderer> g_renderer     = Helpers::CreateScope<Rendering::Renderers::GraphicRenderer>();
    static Helpers::Scope<Hardwares::VulkanDevice>               g_device       = Helpers::CreateScope<Hardwares::VulkanDevice>();
    static ZEngine::Core::Memory::ArenaAllocator                 g_engine_arena = {};

    void                                                         Engine::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, const EngineConfiguration& engine_configuration, ZRawPtr(ZEngine::Windows::CoreWindow) const window)
    {
        arena->CreateSubArena(ZMega(2), &g_engine_arena);

        g_current_window = window;
        Logging::Logger::Initialize(engine_configuration.LoggerConfiguration);
        g_device->Initialize(g_current_window);
        g_renderer->Initialize(g_device.get());

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

                g_current_window->Render(g_renderer.get(), buffer);

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
        return g_current_window;
    }
} // namespace ZEngine
