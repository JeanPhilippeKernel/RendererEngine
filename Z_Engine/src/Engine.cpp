#include "Engine.h"


namespace Z_Engine {

	Engine::Engine()
		: m_event(new SDL_Event{}, [](SDL_Event* e) { free(e); })
	{
		m_window.reset(Z_Engine::Window::Create());
		m_window->SetAttachedEngine(this);

	}
	
	void Engine::ProcessEvent() {
		const auto pending = SDL_PollEvent(m_event.get());
		if (pending) 
		{
			switch (m_event->type)
			{
				case SDL_QUIT:
				{
					WindowClosedEvent e;
					m_window->GetWindowProperty().CallbackFn(e);
				}

				case SDL_WINDOWEVENT:
				{
					switch (m_event->window.event)
					{
						case SDL_WINDOWEVENT_RESIZED:
						{
							Event::WindowResizeEvent e
							{
								static_cast<std::uint32_t>(m_event->window.data1), 
								static_cast<std::uint32_t>(m_event->window.data2)
							};
							m_window->GetWindowProperty().CallbackFn(e);
						}
					}
				}
				case SDL_KEYDOWN :
				{
					if (m_event->key.keysym.sym == SDLK_ESCAPE) {
						Event::WindowClosedEvent e;
						m_window->GetWindowProperty().CallbackFn(e);
					}
				}

				default:
					break;
			}
		}
	}
	
	void Engine::Update() {
		//FPS stuff
		m_window->Update(0.1666f);
		for (auto* const layer : m_layer_stack)
			layer->Update(0.166f);
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
			ProcessEvent();
			Update();
			Render();
		}
	}
}
