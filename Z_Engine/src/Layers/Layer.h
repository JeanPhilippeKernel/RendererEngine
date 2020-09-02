#pragma once
#include <string>
#include "../Event/CoreEvent.h"
#include "../Z_EngineDef.h"
#include "../Core/TimeStep.h"
#include "../Window/CoreWindow.h"


#include "../Core/IInitializable.h"
#include "../Core/IEventable.h"
#include "../Core/IRenderable.h"
#include "../Core/IUpdatable.h"



namespace Z_Engine::Layers {
	class Layer : 
		public Core::IInitializable, 
		public Core::IUpdatable, 
		public Core::IEventable, 
		public Core::IRenderable {
	
	public:
		Layer(const char* name = "default_layer")
			: m_name(name)
		{
		}

		virtual ~Layer() = default;

		const std::string& GetName() const { return m_name; }
		
		virtual void ImGuiRender()  = 0;

		void SetAttachedWindow(const Z_Engine::Ref<Window::CoreWindow>& window) {
			m_window = window;
		}

		Z_Engine::Ref<Z_Engine::Window::CoreWindow>	GetAttachedWindow() const {
			if(!m_window.expired())
				return m_window.lock();
			
			return nullptr;
		}

	private:
		std::string m_name;
		Z_Engine::WeakRef<Z_Engine::Window::CoreWindow> m_window;
	};
}
