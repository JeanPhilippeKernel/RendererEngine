#pragma once
#include <ZEngine/ZEngine.h>

namespace Tetragrama::Layers {
	class UserInterfaceLayer : public ZEngine::Layers::ImguiLayer {
	public:
		UserInterfaceLayer(const char* name = "user interface layer")
			: ImguiLayer(name)
		{
		}
		
		virtual ~UserInterfaceLayer() = default;
	};

}