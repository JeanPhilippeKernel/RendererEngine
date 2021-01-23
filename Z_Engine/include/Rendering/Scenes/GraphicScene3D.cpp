#pragma once
#include "GraphicScene3D.h"


namespace Z_Engine::Rendering::Scenes {

	void GraphicScene3D::Render() {
		Renderers::RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		Renderers::RendererCommand::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GraphicScene::Render();
	}
}