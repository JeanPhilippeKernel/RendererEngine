#include <Engine.h>
#include <Layers/ImguiLayer.h>

#include <SDL_timer.h>

namespace ZEngine {
	
	Core::TimeStep Engine::m_delta_time = { 0.0f };


	Engine::Engine() 
		:m_running(true)
	{
		m_window.reset(ZEngine::Window::Create());
		m_window->SetAttachedEngine(this);
	}

	Engine::~Engine() {
		
	}

	void Engine::Initialize() {
		for(auto layer : m_layer_stack) {
			layer->SetAttachedWindow(m_window);
			layer->Initialize();
		}
	}

	void Engine::ProcessEvent() {
		m_window->PollEvent();
	}
	
	void Engine::Update(Core::TimeStep delta_time) {
		m_window->Update(delta_time);
		for (auto* const layer : m_layer_stack)
			layer->Update(delta_time);
	}
	
	void Engine::Render() {
		for (auto* const layer : m_layer_stack) {
			layer->Render();
		}

		m_window->Render();
	}


	bool Engine::OnEvent(Event::CoreEvent& event) {

		auto index = std::distance(m_layer_stack.begin(), m_layer_stack.end());
		for (auto it = m_layer_stack.end(); it >= m_layer_stack.begin();) {
			
			if(index == 0) break;
			
			(*(--it))->OnEvent(event);
			--index;
		}

		if (!event.IsHandled()) {
			Event::EventDispatcher event_dispatcher(event);
			event_dispatcher.Dispatch<Event::EngineClosedEvent>(std::bind(&Engine::OnEngineClosed, this, std::placeholders::_1));
		}
		return true;
	}

	bool Engine::OnEngineClosed(Event::EngineClosedEvent& event) {
		m_running = false;
		return true;
	}


	
	void Engine::Run() {
		
		while (m_running) {

			float time =  static_cast<float>(SDL_GetTicks()) / 1000.0f;
			m_delta_time = time - m_last_frame_time;
			m_last_frame_time = (m_delta_time >= 1.0f) ? m_last_frame_time : time + 1.0f;	  // waiting 1s to update 
										  		
			ProcessEvent();
			
			if(!m_window->GetWindowProperty().IsMinimized) {
				Update(m_delta_time);
				Render();
			}
		}
	}
}
