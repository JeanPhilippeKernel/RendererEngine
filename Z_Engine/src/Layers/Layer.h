#pragma once
#include <string>
#include "../Event/CoreEvent.h"
#include "../Z_EngineDef.h"

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

		virtual void Initialize() {}
		virtual void Update(float delta_time) {}
		virtual void Render() {}

	public:
		virtual bool OnEvent(Event::CoreEvent&) {
			return false;
		}

	private:
		std::string m_name;
	};
}
