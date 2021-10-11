#include <Window/CoreWindow.h>

using namespace ZEngine;
using namespace ZEngine::Event;
using namespace ZEngine::Layers;

namespace ZEngine::Window {
	const char* CoreWindow::ATTACHED_PROPERTY = "WINDOW_ATTACHED_PROPERTY";

	CoreWindow::CoreWindow() 
		: m_layer_stack_ptr(new LayerStack())
	{
	} 

	void CoreWindow::SetAttachedEngine(Engine* const engine) {
		m_engine = engine;
	}

	void CoreWindow::PushOverlayLayer(Layer* const layer) { 
		m_layer_stack_ptr->PushOverlayLayer(layer); 
	}
		
	void CoreWindow::PushLayer(Layer* const layer) { 
		m_layer_stack_ptr->PushLayer(layer); 
	}	

	void CoreWindow::ForwardEventToLayers(CoreEvent& event) {
		auto index = std::distance(m_layer_stack_ptr->begin(), m_layer_stack_ptr->end());
		for (auto it = m_layer_stack_ptr->end(); it >= m_layer_stack_ptr->begin();) {

			if(index == 0) break;

			(*(--it))->OnEvent(event);
			--index;
		}
	}	
}