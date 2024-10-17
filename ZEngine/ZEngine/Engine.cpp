#include <pch.h>
#include <Engine.h>
#include <Hardwares/VulkanDevice.h>
#include <Logging/LoggerDefinition.h>
#include <Rendering/Renderers/GraphicRenderer.h>

using namespace ZEngine::Rendering::Renderers;

namespace ZEngine
{
    bool                             Engine::m_request_terminate{false};
    float                            Engine::m_last_frame_time{0.0f};
    Core::TimeStep                   Engine::m_delta_time{0.0f};
    Ref<ZEngine::Window::CoreWindow> Engine::m_window{nullptr};

    void Engine::Initialize(const EngineConfiguration& engine_configuration)
    {
        Logging::Logger::Initialize(engine_configuration.LoggerConfiguration);

        m_window.reset(ZEngine::Window::Create(engine_configuration.WindowConfiguration));

        const char** glfw_extensions_layer_name_collection;
        uint32_t     glfw_extensions_layer_name_count = 0;
        glfw_extensions_layer_name_collection         = glfwGetRequiredInstanceExtensions(&glfw_extensions_layer_name_count);
        std::vector<const char*> window_additional_extension_layer_name_collection(
            glfw_extensions_layer_name_collection, glfw_extensions_layer_name_collection + glfw_extensions_layer_name_count);

        Hardwares::VulkanDevice::Initialize(m_window, window_additional_extension_layer_name_collection);

        m_window->Initialize();
        GraphicRenderer::SetMainSwapchain(m_window->GetSwapchain());
        Rendering::Renderers::GraphicRenderer::Initialize();
        /*
         * Renderer Post initialization
         */
        ZENGINE_CORE_INFO("Engine initialized")

        for (const auto& layer : engine_configuration.WindowConfiguration.RenderingLayerCollection)
        {
            m_window->PushLayer(layer);
        }

        for (const auto& layer : engine_configuration.WindowConfiguration.OverlayLayerCollection)
        {
            m_window->PushOverlayLayer(layer);
        }
        m_window->InitializeLayer();
    }

    void Engine::ProcessEvent()
    {
        m_window->PollEvent();
    }

    void Engine::Deinitialize()
    {
        m_window->Deinitialize();
        Rendering::Renderers::GraphicRenderer::Deinitialize();
        Hardwares::VulkanDevice::Deinitialize();
    }

    void Engine::Dispose()
    {
        m_request_terminate = false;
        m_window.reset();

        Hardwares::VulkanDevice::Dispose();

        ZENGINE_CORE_INFO("Engine destroyed")
        Logging::Logger::Dispose();
    }

    void Engine::Update(Core::TimeStep delta_time)
    {
        GraphicRenderer::Update();
        m_window->Update(delta_time);
    }

    void Engine::Render()
    {
        m_window->Render();
    }

    bool Engine::OnEngineClosed(Event::EngineClosedEvent& event)
    {
        m_request_terminate = true;
        return true;
    }

    void Engine::Start()
    {
        m_request_terminate = false;
        Run();
    }

    Ref<ZEngine::Window::CoreWindow> Engine::GetWindow()
    {
        return m_window;
    }

    Core::TimeStep Engine::GetDeltaTime()
    {
        return m_delta_time;
    }

    void Engine::Run()
    {
        while (true)
        {
            if (m_request_terminate)
            {
                break;
            }

            float time        = m_window->GetTime() / 1000.0f;
            m_delta_time      = time - m_last_frame_time;
            m_last_frame_time = (m_delta_time >= 1.0f) ? m_last_frame_time : time + 1.0f; // waiting 1s to update

            ProcessEvent();

            if (m_window->IsMinimized())
            {
                continue;
            }

            Update(m_delta_time);
            Render();
        }

        if (m_request_terminate)
        {
            Deinitialize();
        }
    }
} // namespace ZEngine
