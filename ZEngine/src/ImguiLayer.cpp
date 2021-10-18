#include <Layers/ImguiLayer.h>

namespace ZEngine::Layers {

	bool ImguiLayer::m_initialized = false;

	ImguiLayer::~ImguiLayer() {
		m_ui_components.clear();
		if (m_initialized) {
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplSDL2_Shutdown();
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
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

			ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(m_window.lock()->GetNativeWindow()), m_window.lock()->GetNativeContext());
			ImGui_ImplOpenGL3_Init("#version 460");
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplSDL2_NewFrame(static_cast<SDL_Window*>(m_window.lock()->GetNativeWindow()));
			m_initialized = true;
		}
	}

	void ImguiLayer::AddUIComponent(const Ref<Components::UI::UIComponent>& component) {
		m_ui_components.push_back(component);
	}

	void ImguiLayer::AddUIComponent(Ref<Components::UI::UIComponent>&& component) {
		m_ui_components.emplace_back(component);
	}

	void ImguiLayer::AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components) {
		std::move(std::begin(components), std::end(components), std::back_inserter(m_ui_components));
	}

	void ImguiLayer::Render() {		
		ImGui::NewFrame();
		for (const auto& component : m_ui_components) {
			if(component->GetVisibility() == true) {
				component->Render();
			}
		}
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
		for (int x = 0; x < (int)ImGuiMouseButton_COUNT; ++x)
			io.MouseDown[x] = true;
		return false;
	}

	bool ImguiLayer::OnMouseButtonReleased(Event::MouseButtonReleasedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		for (int x = 0; x < (int)ImGuiMouseButton_COUNT; ++x)
			io.MouseDown[x] = false;

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


	bool ImguiLayer::OnWindowResized(Event::WindowResizedEvent& event) {
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)event.GetWidth(), (float)event.GetHeight());
		io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
		return false;
	}

}