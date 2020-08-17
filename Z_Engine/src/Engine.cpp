#include "Engine.h"
#include "Layers/ImguiLayer.h"


namespace Z_Engine {

	Engine::Engine() 
		:m_running(true)
	{
		m_window.reset(Z_Engine::Window::Create());
		m_window->SetAttachedEngine(this);
		PushOverlayLayer(new Layers::ImguiLayer());
	}

	Engine::~Engine() {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
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

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(static_cast<SDL_Window*>(m_window->GetNativeWindow()));
		ImGui::NewFrame();
		for (auto* const layer : m_layer_stack) {

			layer->ImGuiRender();
			layer->Render();
		}
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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


	void Engine::Initialize() {
		_INITIALIZE_IMGUI_COMPONENT();
	}

	void Engine::_INITIALIZE_IMGUI_COMPONENT() {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();

		ImGuiIO& io = ImGui::GetIO(); (void)io;

		ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(m_window->GetNativeWindow()), m_window->GetNativeContext());
		ImGui_ImplOpenGL3_Init("#version 430");
	}
	
	void Engine::Run() {
		
		while (m_running) {
	
			float time =  static_cast<float>(SDL_GetTicks()) / 1000.0f;
			m_delta_time = time - m_last_frame_time;
			m_last_frame_time = (m_delta_time >= 1.0f) ? m_last_frame_time : time + 1.0f;	  // waiting 1s to update 
										  		
			ProcessEvent();
			Update(m_delta_time);
			Render();

		}
	}
}
