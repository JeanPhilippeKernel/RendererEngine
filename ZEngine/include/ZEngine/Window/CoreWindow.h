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

#include <Core/IInitializable.h>
#include <Core/IUpdatable.h>
#include <Core/IRenderable.h>
#include <Core/IEventable.h>

#include <Window/ICoreWindowEventCallback.h>
#include <Layers/Layer.h>
#include <Layers/LayerStack.h>

namespace ZEngine { class Engine; }
namespace ZEngine::Layers { 
	class Layer; 
	class LayerStack;
}

namespace ZEngine::Window {

	class CoreWindow : 
		public std::enable_shared_from_this<CoreWindow>,
		public Inputs::IKeyboardEventCallback, 
		public Inputs::IMouseEventCallback, 
		public Inputs::ITextInputEventCallback, 
		public Core::IUpdatable, 
		public Core::IRenderable, 
		public Core::IEventable,
		public Core::IInitializable, 
		public ICoreWindowEventCallback  {

	public:
		using EventCallbackFn = std::function<void(Event::CoreEvent&)>;

	public:
		CoreWindow();
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
		
		virtual void PollEvent() = 0;

		virtual void ForwardEventToLayers(Event::CoreEvent& event);
		
		virtual void SetAttachedEngine(ZEngine::Engine* const engine);

		virtual void PushOverlayLayer(Layers::Layer* const layer);
		virtual void PushLayer(Layers::Layer* const layer);

	protected:
		static const char* ATTACHED_PROPERTY;

		WindowProperty m_property;
		ZEngine::Scope<ZEngine::Layers::LayerStack> m_layer_stack_ptr{ nullptr };
		ZEngine::Engine* m_engine { nullptr };	
	};
		
	CoreWindow* Create(WindowProperty prop = {});
}
