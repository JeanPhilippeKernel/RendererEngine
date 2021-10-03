#pragma once
#include <ZEngine/ZEngine.h>

namespace Sandbox3D::Layers {
	class GUILayer : public ZEngine::Layers::ImguiLayer {
	public:
		GUILayer(const char* name = "gui layer")
			: ImguiLayer(name)
		{
		}
		
		virtual ~GUILayer() = default;
	};

}