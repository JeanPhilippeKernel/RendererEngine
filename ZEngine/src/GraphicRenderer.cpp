#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine::Rendering::Renderers {
    GraphicRenderer::GraphicRenderer() : m_renderer_information(new GraphicRendererInformation()) {}

    void GraphicRenderer::AddMesh(Ref<Meshes::Mesh>& mesh) {
        AddMesh(*mesh);
    }

    void GraphicRenderer::AddMesh(std::vector<Meshes::Mesh>& meshes) {
        std::for_each(std::begin(meshes), std::end(meshes), [this](Meshes::Mesh& mesh) { this->AddMesh(mesh); });
    }

    void GraphicRenderer::AddMesh(std::vector<Ref<Meshes::Mesh>>& meshes) {
        std::for_each(std::begin(meshes), std::end(meshes), [this](Ref<Meshes::Mesh>& mesh) { this->AddMesh(mesh); });
    }

    const Ref<Buffers::FrameBuffer>& GraphicRenderer::GetFrameBuffer() const {
        return m_framebuffer;
    }

    const Ref<Rendering::Cameras::Camera>& GraphicRenderer::GetCamera() const {
        return m_camera;
    }

    void GraphicRenderer::StartScene(const Ref<Rendering::Cameras::Camera>& camera) {
        m_camera = camera;
    }

    const Ref<GraphicRendererInformation>& GraphicRenderer::GetRendererInformation() const {
        return m_renderer_information;
    }
} // namespace ZEngine::Rendering::Renderers
