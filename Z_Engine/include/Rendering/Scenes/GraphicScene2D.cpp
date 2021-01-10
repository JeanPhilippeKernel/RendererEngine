#pragma once
#include "GraphicScene2D.h"


namespace Z_Engine::Rendering::Scenes {

	void GraphicScene2D::Render() {
		Renderers::RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		Renderers::RendererCommand::Clear();
		
		GraphicScene::Render();
	}
}