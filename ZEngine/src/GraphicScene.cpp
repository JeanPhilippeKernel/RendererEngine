#include <pch.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Renderers/RenderCommand.h>

using namespace ZEngine::Controllers;

namespace ZEngine::Rendering::Scenes {

    void GraphicScene::Initialize() {
        m_camera_controller->Initialize();
        m_renderer->Initialize();
    }

    const Scope<ICameraController>& GraphicScene::GetCameraController() const {
        return m_camera_controller;
    }

    void GraphicScene::Add(Ref<Meshes::Mesh>& mesh) {
        m_mesh_list.push_back(mesh);
    }

    void GraphicScene::Add(const std::vector<Ref<Meshes::Mesh>>& meshes) {
        std::copy(std::begin(meshes), std::end(meshes), std::back_inserter(m_mesh_list));
    }

    void GraphicScene::UpdateSize(uint32_t width, uint32_t height) {
        m_camera_controller->SetAspectRatio(((float) width) / ((float) height));
        m_camera_controller->UpdateProjectionMatrix();
        m_renderer->GetFrameBuffer()->Resize(width, height);
    }

    void GraphicScene::SetShouldReactToEvent(bool value) {
        m_should_react_to_event = value;
    }
    
    bool GraphicScene::ShouldReactToEvent() const {
        return m_should_react_to_event;
    }

    void GraphicScene::Render() {
        m_renderer->StartScene(m_camera_controller->GetCamera()->GetViewProjectionMatrix());
        m_renderer->AddMesh(m_mesh_list);
        m_renderer->EndScene();

        m_mesh_list.clear();
    }

    unsigned int GraphicScene::ToTextureRepresentation() const {
        return m_renderer->GetFrameBuffer()->GetTexture();
    }

} // namespace ZEngine::Rendering::Scenes
