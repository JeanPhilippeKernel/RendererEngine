#include "ImguiLayer.h"
#include <imgui/imgui.h>

namespace Z_Engine::Layers {

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

	bool ImguiLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		for (int x = 0; x < (int)ImGuiMouseButton_COUNT; ++x)
			io.MouseDown[x] = true;
		return false;
	}

	bool ImguiLayer::OnMouseButtonReleased(MouseButtonReleasedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		for (int x = 0; x < (int)ImGuiMouseButton_COUNT; ++x)
			io.MouseDown[x] = false;

		return false;
	}

	bool ImguiLayer::OnMouseButtonMoved(MouseButtonMovedEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = ImVec2(float(e.GetPosX()), float(e.GetPosY()));

		return false;
	}

	bool ImguiLayer::OnMouseButtonWheelMoved(MouseButtonWheelEvent& e) {
		ImGuiIO& io = ImGui::GetIO();
		if (e.GetOffetX() > 0) io.MouseWheelH += 1;
		if (e.GetOffetX() < 0) io.MouseWheelH -= 1;
		if (e.GetOffetY() > 0) io.MouseWheel += 1;
		if (e.GetOffetY() < 0) io.MouseWheel -= 1;
		return false;
	}

	bool ImguiLayer::OnTextInputRaised(TextInputEvent& event) {
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