#pragma once
#include "Layer.h"
#include "../Core/TimeStep.h"
#include "Event/KeyPressedEvent.h"
#include "Event/KeyReleasedEvent.h"
#include "Event/EventDispatcher.h"
#include "../Inputs/KeyCode.h"


#include <SDL2/SDL_cpuinfo.h>


namespace Z_Engine::Layers {
	class ImguiLayer : public Layer {
	public:
		ImguiLayer(const char * name = "ImGUI Layer")
			: Layer(name)
		{
		}

		~ImguiLayer() = default;

		void Initialize() override {}

		bool OnEvent(Event::CoreEvent& event) override {
			EventDispatcher event_dispatcher(event);

			event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&ImguiLayer::OnKeyPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&ImguiLayer::OnKeyReleased, this, std::placeholders::_1));
			
			event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&ImguiLayer::OnMouseButtonPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&ImguiLayer::OnMouseButtonReleased, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&ImguiLayer::OnMouseButtonMoved, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&ImguiLayer::OnMouseButtonWheelMoved, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&ImguiLayer::OnTextInputRaised, this, std::placeholders::_1));

			return false;
		}

		void Update(Core::TimeStep dt) override { }

		virtual void ImGuiRender() override {

			//static int counter = 1;
			//// get the window size as a base for calculating widgets geometry
			//int sdl_width = 0, sdl_height = 0, controls_width = 0;
			////SDL_GetWindowSize(window, &sdl_width, &sdl_height);
			//controls_width = 1080;
			//// make controls widget width to be 1/3 of the main window width
			//if ((controls_width /= 3) < 300) { controls_width = 300; }

			//// position the controls widget in the top-right corner with some margin
			//ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
			//// here we set the calculated width and also make the height to be
			//// be the height of the main window also with some margin
			//ImGui::SetNextWindowSize(
			//	ImVec2(static_cast<float>(controls_width), static_cast<float>(sdl_height - 20)),
			//	ImGuiCond_Always
			//);
			//// create a window and append into it
			//ImGui::Begin("Controls", NULL, ImGuiWindowFlags_NoResize);

			//ImGui::Dummy(ImVec2(0.0f, 1.0f));
			//ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "Platform");
			//ImGui::Text("%s", SDL_GetPlatform());
			//ImGui::Text("CPU cores: %d", SDL_GetCPUCount());
			//ImGui::Text("RAM: %.2f GB", SDL_GetSystemRAM() / 1024.0f);

			//// buttons and most other widgets return true when clicked/edited/activated
			//if (ImGui::Button("Counter button"))
			//{

			//	//std::cout << "counter button clicked\n";
			//	counter++;
			//}
			//ImGui::SameLine();
			//ImGui::Text("counter = %d", counter);
			//ImGui::End();


			//ImGui::ShowDemoWindow(&m_show);
		}

		void Render() override {}

	protected:
		bool OnKeyPressed(Event::KeyPressedEvent& e) { 
			ImGuiIO& io = ImGui::GetIO();
			io.KeysDown[(int)e.GetKeyCode()] = true;
			return false; 
		}

		bool OnKeyReleased(Event::KeyReleasedEvent& e) {
			ImGuiIO& io = ImGui::GetIO();
			io.KeysDown[(int)e.GetKeyCode()] = false;
			return false; 
		}

		bool OnMouseButtonPressed(MouseButtonPressedEvent& e) {
			ImGuiIO& io = ImGui::GetIO();
			for (int x = 0; x < (int)ImGuiMouseButton_COUNT; ++x)
				io.MouseDown[x] = true;		
			return false;
		}
		
		bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) {
			ImGuiIO& io = ImGui::GetIO();
			for (int x = 0; x < (int)ImGuiMouseButton_COUNT; ++x)
				io.MouseDown[x] = false;
			
			return false;
		}
		
		bool OnMouseButtonMoved(MouseButtonMovedEvent& e) {
			ImGuiIO& io = ImGui::GetIO();
			io.MousePos =  ImVec2(float(e.GetPosX()), float(e.GetPosY()));
			
			return false;
		}
		
		bool OnMouseButtonWheelMoved(MouseButtonWheelEvent& e) {
			ImGuiIO& io = ImGui::GetIO();
			if (e.GetOffetX() > 0) io.MouseWheelH += 1;
			if (e.GetOffetX() < 0) io.MouseWheelH -= 1;
			if (e.GetOffetY() > 0) io.MouseWheel += 1;
			if (e.GetOffetY() < 0) io.MouseWheel -= 1;
			return false;
		}

		bool OnTextInputRaised(TextInputEvent& event) {
			ImGuiIO& io = ImGui::GetIO();
			for(unsigned char c : event.GetText()) io.AddInputCharacter(c);
			return false;
		}

	private:
		  bool m_show{true};
	};
}
						  