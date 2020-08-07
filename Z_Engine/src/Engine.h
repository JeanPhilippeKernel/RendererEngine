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


#include <imgui/imgui.h>
#include <imgui/imconfig.h>
#include <imgui/examples/imgui_impl_sdl.h>

#define IMGUI_IMPL_OPENGL_LOADER_GLEW

#include <imgui/examples/imgui_impl_opengl3.h>

namespace Z_Engine {
	
	class Z_ENGINE_API Engine {
	public:
		Engine();
		virtual ~Engine() {
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplSDL2_Shutdown();
			ImGui::DestroyContext();

		}
		
		void InitializeComponents();
		
		void Run();

		const Ref<Z_Engine::Window::CoreWindow>& GetWindow() const { return m_window; }
		
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
		Ref<Z_Engine::Window::CoreWindow> m_window;


		void _INITIALIZE_IMGUI_COMPONENT();
	};


	Engine* CreateEngine();
}




