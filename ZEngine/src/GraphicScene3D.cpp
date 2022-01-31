#include <pch.h>
#include <Rendering/Scenes/GraphicScene3D.h>
#include <Rendering/Renderers/RenderCommand.h>

namespace ZEngine::Rendering::Scenes {

    void GraphicScene3D::Render() {
        m_renderer->GetFrameBuffer()->Bind();
        Renderers::RendererCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1.0f});
        Renderers::RendererCommand::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        GraphicScene::Render();
        m_renderer->GetFrameBuffer()->Unbind();
    }
} // namespace ZEngine::Rendering::Scenes
