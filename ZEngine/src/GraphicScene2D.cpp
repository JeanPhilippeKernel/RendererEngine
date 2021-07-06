#include <Rendering/Scenes/GraphicScene2D.h>


namespace ZEngine::Rendering::Scenes {

	void GraphicScene2D::Render() {
		Renderers::RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		Renderers::RendererCommand::Clear();
		
		GraphicScene::Render();
	}
}