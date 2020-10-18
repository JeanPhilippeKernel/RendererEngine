#pragma once
#include <memory>

#include "Z_EngineDef.h"
#include "Window/CoreWindow.h"
#include "Event/EventDispatcher.h"
#include "Event/EngineClosedEvent.h"
#include "Event/KeyPressedEvent.h"
#include "Event/KeyReleasedEvent.h"

#include "LayerStack.h"
#include "Core/TimeStep.h"

#include "Core/IUpdatable.h"
#include "Core/IRenderable.h"
#include "Core/IEventable.h"
#include "Core/IInitializable.h"


#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/imconfig.h"
#include "../vendor/imgui/examples/imgui_impl_sdl.h"

#define IMGUI_IMPL_OPENGL_LOADER_GLEW

#include "../vendor/imgui/examples/imgui_impl_opengl3.h"

namespace Z_Engine {
	
	class Engine : 
		public Core::IInitializable,
		public Core::IUpdatable, 
		public Core::IRenderable, 
		public Core::IEventable {

	public:
		Engine();
		virtual ~Engine();

	public:
		virtual void Initialize() override;
		virtual void Update(Core::TimeStep delta_time) override;
		virtual void Render() override;
		virtual bool OnEvent(Event::CoreEvent&) override;
		
	public:
		void Run();

		const Ref<Z_Engine::Window::CoreWindow>& GetWindow() const { return m_window; }
		void PushOverlayLayer(Layer* const layer) { m_layer_stack.PushOverlayLayer(layer); }
		void PushLayer(Layer* const layer) { m_layer_stack.PushLayer(layer); }

	protected:
		virtual void ProcessEvent();


	public:
		bool OnEngineClosed(Event::EngineClosedEvent&);
	
	protected:
		LayerStack m_layer_stack;
	
	private:
		bool m_running{ false };
		Core::TimeStep m_delta_time { 0.0f };
		float m_last_frame_time { 0.0f };
		Ref<Z_Engine::Window::CoreWindow> m_window;

	private:
		void _INITIALIZE_IMGUI_COMPONENT();

	};


	Engine* CreateEngine();
}




