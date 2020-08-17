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
	class Z_ENGINE_API Layer : 
		public Core::IInitializable, 
		public Core::IUpdatable, 
		public Core::IEventable, 
		public Core::IRenderable  {
	
	public:
		Layer(const char* name = "default_layer")
			: m_name(name)
		{
		}

		virtual ~Layer() = default;

		const std::string& GetName() const { return m_name; }
		
		virtual void ImGuiRender()  = 0;

	private:
		std::string m_name;
	};
}
