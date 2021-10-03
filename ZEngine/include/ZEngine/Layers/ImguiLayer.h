#pragma once
#include <Layers/Layer.h>

#include <imgui.h>
#include <imconfig.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_opengl3.h>

#include <Core/TimeStep.h>
#include <Inputs/KeyCode.h>

#include <Inputs/IKeyboardEventCallback.h>
#include <Inputs/IMouseEventCallback.h>
#include <Inputs/ITextInputEventCallback.h>
#include <Window/ICoreWindowEventCallback.h>
#include <Components/UIComponent.h>

#include <functional>
#include <vector>

namespace ZEngine::Layers {
	class ImguiLayer : public Layer, 
		public Inputs::IKeyboardEventCallback, 
		public Inputs::IMouseEventCallback, 
		public Inputs::ITextInputEventCallback, 
		public Window::ICoreWindowEventCallback {

	public:
		ImguiLayer(const char * name = "ImGUI Layer")
			: Layer(name)
		{
		}

		virtual ~ImguiLayer();

		void Initialize() override;

		bool OnEvent(Event::CoreEvent& event) override {
			EventDispatcher event_dispatcher(event);

			event_dispatcher.Dispatch<Event::KeyPressedEvent>(std::bind(&ImguiLayer::OnKeyPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::KeyReleasedEvent>(std::bind(&ImguiLayer::OnKeyReleased, this, std::placeholders::_1));
			
			event_dispatcher.Dispatch<Event::MouseButtonPressedEvent>(std::bind(&ImguiLayer::OnMouseButtonPressed, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonReleasedEvent>(std::bind(&ImguiLayer::OnMouseButtonReleased, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonMovedEvent>(std::bind(&ImguiLayer::OnMouseButtonMoved, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::MouseButtonWheelEvent>(std::bind(&ImguiLayer::OnMouseButtonWheelMoved, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Event::TextInputEvent>(std::bind(&ImguiLayer::OnTextInputRaised, this, std::placeholders::_1));
			
			event_dispatcher.Dispatch<Event::WindowResizedEvent>(std::bind(&ImguiLayer::OnWindowResized, this, std::placeholders::_1));

			return false;
		}

		void Update(Core::TimeStep dt) override { }
		
		virtual void AddUIComponent(const Ref<Components::UI::UIComponent>& component);
		virtual void AddUIComponent(Ref<Components::UI::UIComponent>&& component);
		virtual void AddUIComponent(std::vector<Ref<Components::UI::UIComponent>>&& components);
		
		void Render() override;

	protected:
		bool OnKeyPressed(Event::KeyPressedEvent&)						override;
		bool OnKeyReleased(Event::KeyReleasedEvent&)					override;

		bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&)		override;
		bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&)	override;
		bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&)			override;
		bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&)		override;
		bool OnTextInputRaised(Event::TextInputEvent&)					override;

		bool OnWindowClosed(Event::WindowClosedEvent&)					override { return false; }
		bool OnWindowResized(Event::WindowResizedEvent&)				override;
		bool OnWindowMinimized(Event::WindowMinimizedEvent&)			override { return false; }
		bool OnWindowMaximized(Event::WindowMaximizedEvent&)			override { return false; }
		bool OnWindowRestored(Event::WindowRestoredEvent&)				override { return false; }

	private:
		static bool m_initialized;
		std::vector<Ref<Components::UI::UIComponent>> m_ui_components;	  
	};
}
						  