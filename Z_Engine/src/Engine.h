#pragma once
#include <memory>
#include <SDL2/SDL_events.h>

#include "Z_EngineDef.h"
#include "Window/CoreWindow.h"
#include "Event/EventDispatcher.h"
#include "Event/EngineClosedEvent.h"
#include "Event/KeyPressedEvent.h"
#include "Event/KeyReleasedEvent.h"

#include "LayerStack.h"


namespace Z_Engine {
	
	class Z_ENGINE_API Engine {
	public:
		Engine();
		virtual ~Engine() = default;

		void Run();

	protected:
		virtual void ProcessEvent();
		virtual void Update();
		virtual void Render();


	protected:
		bool OnEngineClosed(Event::EngineClosedEvent&);

	public:
		virtual bool OnEvent(Event::CoreEvent&);
	
	protected:
		LayerStack m_layer_stack;
	
	private:
		bool m_running{ true };
		
		std::unique_ptr<Z_Engine::Window::CoreWindow>	m_window;
		std::unique_ptr<SDL_Event, std::function<void(SDL_Event *)>> m_event;
	};


	Engine* CreateEngine();
}




