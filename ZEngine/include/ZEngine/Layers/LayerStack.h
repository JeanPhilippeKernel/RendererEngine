#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <Layers/Layer.h>

namespace ZEngine::Layers {
	class Layer;

	class LayerStack {
	public:
		LayerStack() = default;
		~LayerStack();

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