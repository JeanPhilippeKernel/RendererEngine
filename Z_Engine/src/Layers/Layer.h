#pragma once
#include <string>
#include "../Event/CoreEvent.h"
#include "../Z_EngineDef.h"
#include "Core/TimeStep.h"

namespace Z_Engine::Layers {
	class Z_ENGINE_API Layer {
	public:
		Layer(const char* name = "default_layer")
			: m_name(name)
		{
		}

		virtual ~Layer() = default;

		const std::string& GetName() const {
			return m_name;
		}

		virtual void Initialize() = 0;
		virtual void Update(Core::TimeStep dt) = 0;
		virtual void Render()  = 0;

	public:
		virtual bool OnEvent(Event::CoreEvent&) = 0;

	private:
		std::string m_name;
	};
}
