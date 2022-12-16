#include <pch.h>
#include <Engine.h>
#include <Layers/ImguiLayer.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine
{
    Core::TimeStep Engine::m_delta_time = {0.0f};

    Engine::Engine(const EngineConfiguration& configuration)
        : m_engine_configuration(configuration), m_request_terminate(false), m_last_frame_time(0.0f), m_vulkan_instance("ZEngine")
    {
    }

    Engine::~Engine()
    {
        m_request_terminate = false;
        m_window.reset();
        ZENGINE_CORE_INFO("Engine destroyed")
        Logging::Logger::Flush();
    }

    void Engine::Initialize()
    {
        Logging::Logger::Initialize(m_engine_configuration.LoggerConfiguration);

        m_vulkan_instance.CreateInstance();

        m_window.reset(ZEngine::Window::Create(m_engine_configuration.WindowConfiguration));

        for (const auto& layer : m_engine_configuration.WindowConfiguration.RenderingLayerCollection)
        {
            m_window->PushLayer(layer);
        }

        for (const auto& layer : m_engine_configuration.WindowConfiguration.OverlayLayerCollection)
        {
            m_window->PushOverlayLayer(layer);
        }

        m_window->SetAttachedEngine(this);
        m_window->Initialize();

        ZENGINE_CORE_INFO("Engine initialized")
    }

    void Engine::ProcessEvent()
    {
        m_window->PollEvent();
    }

    void Engine::Deinitialize()
    {
        m_window->Deinitialize();
    }

    void Engine::Update(Core::TimeStep delta_time)
    {
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

    Hardwares::VulkanInstance& Engine::GetVulkanInstance()
    {
        return m_vulkan_instance;
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
