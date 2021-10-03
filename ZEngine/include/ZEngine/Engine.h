#pragma once

#include <ZEngineDef.h>
#include <Window/CoreWindow.h>
#include <Event/EventDispatcher.h>
#include <Event/EngineClosedEvent.h>
#include <Event/KeyPressedEvent.h>
#include <Event/KeyReleasedEvent.h>

#include <LayerStack.h>
#include <Core/TimeStep.h>

#include <Core/IUpdatable.h>
#include <Core/IRenderable.h>
#include <Core/IEventable.h>
#include <Core/IInitializable.h>


namespace ZEngine {
	
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

		const Ref<ZEngine::Window::CoreWindow>& GetWindow() const { return m_window; }
		void PushOverlayLayer(Layer* const layer) { m_layer_stack.PushOverlayLayer(layer); }
		void PushLayer(Layer* const layer) { m_layer_stack.PushLayer(layer); }

		static Core::TimeStep GetDeltaTime() { return m_delta_time; }

	protected:
		virtual void ProcessEvent();


	public:
		bool OnEngineClosed(Event::EngineClosedEvent&);
	
	protected:
		LayerStack m_layer_stack;
	
	private:
		static Core::TimeStep m_delta_time;
		
		bool m_running{ false };
		float m_last_frame_time { 0.0f };
		Ref<ZEngine::Window::CoreWindow> m_window;

	};


	Engine* CreateEngine();
}




