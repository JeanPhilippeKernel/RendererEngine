#include <Layers/ImguiLayer.h>
#include <algorithm>
#include <ZEngineDef.h>

#ifdef ZENGINE_WINDOW_SDL
#include <SDL2/include/SDL.h>
#else
#include <GLFW/glfw3.h>
#endif

namespace ZEngine::Layers {

	bool ImguiLayer::m_initialized = false;

	ImguiLayer::~ImguiLayer() {
		m_ui_components.clear();
		if (m_initialized) {
			ImGui_ImplOpenGL3_Shutdown();

#ifdef ZENGINE_WINDOW_SDL
			ImGui_ImplSDL2_Shutdown();
#else
			ImGui_ImplGlfw_Shutdown();
#endif
			ImGui::DestroyContext();
		
			m_initialized = false;
		}
	}

	void ImguiLayer::Initialize() {
		if (!m_initialized) {
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGui::StyleColorsDark();

			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

#ifdef ZENGINE_WINDOW_SDL
			ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(m_window.lock()->GetNativeWindow()), m_window.lock()->GetNativeContext());
#else
			ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(m_window.lock()->GetNativeWindow()), false);
#endif
			
			// we should get Version information from attached Window...
#ifdef _WIN32
			ImGui_ImplOpenGL3_Init("#version 450");
#else
			ImGui_ImplOpenGL3_Init("#version 330");
#endif
			m_initialized = true;
		}
	}


	void ImguiLayer::Update(Core::TimeStep dt) {
		ImGuiIO& io = ImGui::GetIO();
		if (dt > 0.0f) {
			io.DeltaTime = dt;
		}
	}
	
	void ImguiLayer::AddUIComponent(const Ref<Components::UI::UIComponent>& component) {
		m_ui_components.push_back(component);
		
		if (!component->HasParentLayer()) {
			auto last = std::prev(std::end(m_ui_components));
			(*last)->SetParentLayer(shared_from_this());
		}
	}

	void ImguiLayer::AddUIComponent(Ref<Components::UI::UIComponent>&& component) {
		if (!component->HasParentLayer()) {
			component->SetParentLayer(shared_from_this());
		}
		m_ui_components.push_back(component);
	}

	void ImguiLayer::AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components) {
		std::for_each(std::begin(components), std::end(components), [this](Ref<Components::UI::UIComponent>& component) {
			if (!component->HasParentLayer()) {
				component->SetParentLayer(shared_from_this());
			}
		});

		std::move(std::begin(components), std::end(components), std::back_inserter(m_ui_components));
	}

	void ImguiLayer::Render() {		
		ImGui_ImplOpenGL3_NewFrame();
#ifdef ZENGINE_WINDOW_SDL
		ImGui_ImplSDL2_NewFrame(static_cast<SDL_Window*>(m_window.lock()->GetNativeWindow()));
#else
		ImGui_ImplGlfw_NewFrame();
#endif

		ImGui::NewFrame();
		for (const auto& component : m_ui_components) {
			if(component->GetVisibility() == true) {
				component->Render();
			}
		}

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
#ifdef ZENGINE_WINDOW_SDL
			SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
			SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
#else
			GLFWwindow* backup_current_context = static_cast<GLFWwindow*>(m_window.lock()->GetNativeContext());
#endif
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

#ifdef ZENGINE_WINDOW_SDL
			SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
#else
			glfwMakeContextCurrent(backup_current_context);
#endif
		}
	}

	bool ImguiLayer::OnKeyPressed(Event::KeyPressedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();	
		io.KeysDown[(int)e.GetKeyCode()] = true;
		return false;
	}

	bool ImguiLayer::OnKeyReleased(Event::KeyReleasedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		io.KeysDown[(int)e.GetKeyCode()] = false;
		return false;
	}

	bool ImguiLayer::OnMouseButtonPressed(Event::MouseButtonPressedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[(uint32_t)e.GetButton()] = true;
		return false;
	}

	bool ImguiLayer::OnMouseButtonReleased(Event::MouseButtonReleasedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[(int)e.GetButton()] = false;
		return false;
	}

	bool ImguiLayer::OnMouseButtonMoved(Event::MouseButtonMovedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = ImVec2(float(e.GetPosX()), float(e.GetPosY()));
		return false;
	}

	bool ImguiLayer::OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		if (e.GetOffetX() > 0) io.MouseWheelH += 1;
		if (e.GetOffetX() < 0) io.MouseWheelH -= 1;
		if (e.GetOffetY() > 0) io.MouseWheel += 1;
		if (e.GetOffetY() < 0) io.MouseWheel -= 1;
		return false;
	}

	bool ImguiLayer::OnTextInputRaised(Event::TextInputEvent& event) {
		ImGuiIO& io = ImGui::GetIO();
		for (unsigned char c : event.GetText()) io.AddInputCharacter(c);
		return false;
	}

	bool ImguiLayer::OnWindowClosed(Event::WindowClosedEvent& event) {
		Event::EventDispatcher event_dispatcher(event);
		event_dispatcher.ForwardTo<Event::WindowClosedEvent>(std::bind(&ZEngine::Window::CoreWindow::OnWindowClosed, GetAttachedWindow().get(), std::placeholders::_1));
		return true;
	}

	bool ImguiLayer::OnWindowResized(Event::WindowResizedEvent& event) {
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)event.GetWidth(), (float)event.GetHeight());
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
		return false;
	}


}