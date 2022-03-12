#include <pch.h>
#include <Engine.h>
#include <Layers/ImguiLayer.h>
#include <Logging/LoggerDefinition.h>

namespace ZEngine {

    Core::TimeStep Engine::m_delta_time = {0.0f};

    Engine::Engine() : m_request_terminate(false), m_last_frame_time(0.0f) {
        Logging::Logger::Initialize();
        ZENGINE_CORE_INFO("Engine started");

        m_window.reset(ZEngine::Window::Create());
        m_window->SetAttachedEngine(this);
    }

    Engine::~Engine() {
        m_request_terminate = false;
        ZENGINE_CORE_INFO("Engine stopped");
    }

    void Engine::Initialize() {
        if (!m_window->HasContext()) {
            m_window->CreateAndActiveContext();
            m_window->Initialize();
        }
        ZENGINE_CORE_INFO("Engine initialized");
    }

    void Engine::ProcessEvent() {
        m_window->PollEvent();
    }

    void Engine::Update(Core::TimeStep delta_time) {
        m_window->Update(delta_time);
    }

    void Engine::Render() {
        m_window->Render();
    }

    bool Engine::OnEngineClosed(Event::EngineClosedEvent& event) {
        m_request_terminate = true;
        return true;
    }

    void Engine::Start() {
        m_request_terminate = false;
        Run();
    }

    void Engine::Run() {

        while (true) {

            if (m_request_terminate) {
                break;
            }

            float time        = m_window->GetTime() / 1000.0f;
            m_delta_time      = time - m_last_frame_time;
            m_last_frame_time = (m_delta_time >= 1.0f) ? m_last_frame_time : time + 1.0f; // waiting 1s to update

            ProcessEvent();
            if (!m_window->GetWindowProperty().IsMinimized) {
                Update(m_delta_time);
                Render();
            }
        }
    }
} // namespace ZEngine
