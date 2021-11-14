#include <Rendering/Scenes/GraphicScene2D.h>


namespace ZEngine::Rendering::Scenes {

	void GraphicScene2D::Render() {
		m_renderer->GetFrameBuffer()->Bind();
		Renderers::RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		Renderers::RendererCommand::Clear();		
		GraphicScene::Render();	  
		m_renderer->GetFrameBuffer()->Unbind();
	}
}