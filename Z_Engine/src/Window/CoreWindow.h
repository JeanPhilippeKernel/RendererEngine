#pragma once

#include <memory>

#include "WindowProperty.h"
#include "../Event/CoreEvent.h"
#include "../Event/WindowClosedEvent.h"
#include "../Event/WindowResizeEvent.h"
#include "../Event/KeyPressedEvent.h"
#include "../Event/KeyReleasedEvent.h"
#include "../Event/EventDispatcher.h"
#include "../Core/TimeStep.h"

//#include <SDL2/SDL_keyboard.h>
//#include <SDL2/SDL_events.h>


namespace Z_Engine {
	class Engine;
}


using namespace Z_Engine::Event;

namespace Z_Engine::Window {


	class CoreWindow {

	public:
		using EventCallbackFn = std::function<void(CoreEvent&)>;

	public:
		CoreWindow() = default;
		virtual ~CoreWindow() { delete m_last_raised_event; }

		virtual unsigned int GetHeight()	const = 0;
		virtual unsigned int GetWidth()		const = 0;
		virtual const std::string& GetTitle()		const = 0;
		virtual bool IsVSyncEnable()		const = 0;
		virtual	void SetVSync(bool value) = 0;
		virtual void SetCallbackFunction(const EventCallbackFn& callback) = 0;

		virtual void* GetNativeWindow() const = 0;

		virtual const WindowProperty& GetWindowProperty() const = 0;
		
		virtual void SetAttachedEngine(Z_Engine::Engine* const engine) {
			m_engine = engine;
		}


		const Event::CoreEvent* GetLastEvent() const { return m_last_raised_event; }

		virtual void PollEvent() = 0;
		virtual void Update(Core::TimeStep delta_time) = 0;
		virtual void Render() = 0;


	//public:
	//	static bool IsKeyPressed(Z_Engine::Core::Input::KeyCode key) {
	//		const auto* state = SDL_GetKeyboardState(NULL);
	//		return state[(int)key] == SDL_PRESSED;			   
	//	}

	//	static bool IsKeyReleased(Z_Engine::Core::Input::KeyCode key) {
	//		const auto* state = SDL_GetKeyboardState(NULL);
	//		return state[(int)key] == SDL_RELEASED;
	//	}

	public:
		virtual void OnEvent(Event::CoreEvent& event) = 0;

	protected:
		virtual bool OnWindowClosed(WindowClosedEvent&) = 0;
		virtual bool OnWindowResized(WindowResizeEvent&) = 0;

		virtual bool OnKeyPressed(KeyPressedEvent&) = 0;
		virtual bool OnKeyReleased(KeyReleasedEvent&) = 0;

	protected:
		static const char* ATTACHED_PROPERTY;

		WindowProperty m_property;
		Z_Engine::Engine* m_engine;
		Event::CoreEvent* m_last_raised_event;
	};
		
	CoreWindow* Create(WindowProperty prop = {});

}
