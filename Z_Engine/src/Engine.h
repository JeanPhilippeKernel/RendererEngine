#pragma once
#include <memory>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_timer.h>

#include "Z_EngineDef.h"
#include "Window/CoreWindow.h"
#include "Event/EventDispatcher.h"
#include "Event/EngineClosedEvent.h"
#include "Event/KeyPressedEvent.h"
#include "Event/KeyReleasedEvent.h"

#include "LayerStack.h"
#include "Core/TimeStep.h"


namespace Z_Engine {
	
	class Z_ENGINE_API Engine {
	public:
		Engine();
		virtual ~Engine() = default;

		void Run();

		//const std::unique_ptr<Z_Engine::Window::CoreWindow>& GetWindow() const { return m_window; }
		
		void PushOverlayLayer(Layer* const layer) { m_layer_stack.PushOverlayLayer(layer); }
		void PushLayer(Layer* const layer) { m_layer_stack.PushLayer(layer); }

	protected:
		virtual void ProcessEvent();
		virtual void Update(Core::TimeStep delta_time);
		virtual void Render();


	public:
		bool OnEngineClosed(Event::EngineClosedEvent&);
		virtual bool OnEvent(Event::CoreEvent&);
	
	protected:
		LayerStack m_layer_stack;
	
	private:
		bool m_running{ true };
		float m_last_frame_time {0.0f};
		Ref<Z_Engine::Window::CoreWindow>	m_window;
	};


	Engine* CreateEngine();
}




