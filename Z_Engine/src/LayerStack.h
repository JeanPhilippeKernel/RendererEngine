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

		void PushLayer(Layer* const layer) {
			if (m_layers.empty()) {
				m_layers.emplace_back(layer);
				current_it = std::begin(m_layers);
			}
			else {
				current_it = m_layers.emplace(current_it, layer);
			}
	
			layer->Initialize();
		}

		void PushOverlayLayer(Layer* const layer) {
			m_layers.push_back(layer);
			layer->Initialize();
			
			if (m_layers.size() == 1) {
				current_it = std::begin(m_layers);
			}
		}
		
		void PopLayer(Layer* const layer) {
			const auto it = std::find_if(
				std::begin(m_layers), std::end(m_layers),
				[layer](Layer* const x) { return x == layer; }
			);

			if (it != std::end(m_layers)) {
				current_it = m_layers.erase(it);
			}
		}

		void PopLayer() {
			const auto it = std::begin(m_layers);
			if (it != std::end(m_layers)) {
				current_it = m_layers.erase(it);
			}
		}



		void PopOverlayLayer() {
			const auto it = std::end(m_layers) - 1;
			if (it != std::end(m_layers)) {
				m_layers.erase(it);
			}
		}


		void PopOverlayLayer(Layer* const layer) {
			const auto it = std::find_if(
				std::begin(m_layers), std::end(m_layers),
				[layer](Layer* const x) { return x == layer; }
			);

			if (it != std::end(m_layers)) {
				m_layers.erase(it);
			}
		}

	public:
		std::vector<Layer*>::iterator begin() { return std::begin(m_layers); }
		std::vector<Layer*>::iterator end() { return std::end(m_layers); }

	private:
		std::vector<Layer*> m_layers;
		std::vector<Layer*>::iterator current_it;
	};
}