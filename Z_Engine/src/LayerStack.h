#pragma once
#include <vector>
#include "Z_EngineDef.h"
#include "Layers/Layer.h"

using namespace Z_Engine::Layers;

namespace Z_Engine {
	
	class Z_ENGINE_API LayerStack {
	public:
		LayerStack() = default;
		~LayerStack() = default;

		void PushLayer(Layer* const layer);

		void PushOverlayLayer(Layer* const layer);
		
		void PopLayer(Layer* const layer);

		void PopLayer();

		void PopOverlayLayer();

		void PopOverlayLayer(Layer* const layer);

	public:
		std::vector<Layer*>::iterator begin() { return std::begin(m_layers); }
		std::vector<Layer*>::iterator end() { return std::end(m_layers); }

	private:
		std::vector<Layer*> m_layers;
		std::vector<Layer*>::iterator current_it;
	};
}