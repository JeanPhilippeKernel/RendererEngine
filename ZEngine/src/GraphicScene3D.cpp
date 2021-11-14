#include <Rendering/Scenes/GraphicScene3D.h>


namespace ZEngine::Rendering::Scenes {

	void GraphicScene3D::Render() {
		m_renderer->GetFrameBuffer()->Bind();
		Renderers::RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		Renderers::RendererCommand::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		GraphicScene::Render();
		m_renderer->GetFrameBuffer()->Unbind();
	}
}