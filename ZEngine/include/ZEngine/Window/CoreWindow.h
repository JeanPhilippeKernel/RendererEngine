#pragma once

#include <memory>

#include <Window/WindowProperty.h>
#include <Event/CoreEvent.h>
#include <Event/WindowClosedEvent.h>
#include <Event/WindowResizedEvent.h>
#include <Event/EventDispatcher.h>
#include <Event/TextInputEvent.h>
#include <Core/TimeStep.h>

#include <Inputs/IKeyboardEventCallback.h>
#include <Inputs/IMouseEventCallback.h>
#include <Inputs/ITextInputEventCallback.h>


#include <Core/IUpdatable.h>
#include <Core/IRenderable.h>
#include <Core/IEventable.h>

#include <Window/ICoreWindowEventCallback.h>

namespace ZEngine {
	class Engine;
}


using namespace ZEngine::Event;

namespace ZEngine::Window {


	class CoreWindow : 
		public Inputs::IKeyboardEventCallback, 
		public Inputs::IMouseEventCallback, 
		public Inputs::ITextInputEventCallback, 
		public Core::IUpdatable, 
		public Core::IRenderable, 
		public Core::IEventable, 
		public ICoreWindowEventCallback  {

	public:
		using EventCallbackFn = std::function<void(CoreEvent&)>;

	public:
		CoreWindow() = default;
		virtual ~CoreWindow() = default;

		virtual unsigned int GetHeight()	const = 0;
		virtual unsigned int GetWidth()		const = 0;
		virtual const std::string& GetTitle()		const = 0;
		virtual bool IsVSyncEnable()		const = 0;
		virtual	void SetVSync(bool value) = 0;
		virtual void SetCallbackFunction(const EventCallbackFn& callback) = 0;

		virtual void* GetNativeWindow() const = 0;
		virtual void* GetNativeContext() const = 0;

		virtual const WindowProperty& GetWindowProperty() const = 0;
		
		virtual void SetAttachedEngine(ZEngine::Engine* const engine) {
			m_engine = engine;
		}

		virtual void PollEvent() = 0;

	protected:
		static const char* ATTACHED_PROPERTY;

		WindowProperty m_property;
		ZEngine::Engine* m_engine { nullptr };
	};
		
	CoreWindow* Create(WindowProperty prop = {});
}
