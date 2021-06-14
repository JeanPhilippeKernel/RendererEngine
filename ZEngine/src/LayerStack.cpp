#include <LayerStack.h>


namespace ZEngine {
	
	LayerStack::~LayerStack() {
		for(auto layer : m_layers)
			delete layer;
	}


	void LayerStack::PushLayer(Layer* const layer) {
		if (m_layers.empty()) {
			m_layers.emplace_back(layer);
			current_it = std::begin(m_layers);
		}
		else {
			current_it = m_layers.emplace(current_it, layer);
		}

		//layer->Initialize();
	}

	void LayerStack::PushOverlayLayer(Layer* const layer) {
		m_layers.push_back(layer);
		//layer->Initialize();

		if (m_layers.size() == 1) {
			current_it = std::begin(m_layers);
		}
	}

	void LayerStack::PopLayer(Layer* const layer) {
		const auto it = std::find_if(
			std::begin(m_layers), std::end(m_layers),
			[layer](Layer* const x) { return x == layer; }
		);

		if (it != std::end(m_layers)) {
			current_it = m_layers.erase(it);
		}
	}

	void LayerStack::PopLayer() {
		const auto it = std::begin(m_layers);
		if (it != std::end(m_layers)) {
			current_it = m_layers.erase(it);
		}
	}



	void LayerStack::PopOverlayLayer() {
		const auto it = std::end(m_layers) - 1;
		if (it != std::end(m_layers)) {
			m_layers.erase(it);
		}
	}


	void LayerStack::PopOverlayLayer(Layer* const layer) {
		const auto it = std::find_if(
			std::begin(m_layers), std::end(m_layers),
			[layer](Layer* const x) { return x == layer; }
		);

		if (it != std::end(m_layers)) {
			m_layers.erase(it);
		}
	}


}