#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/RenderCommand.h>

namespace ZEngine::Rendering::Renderers {
    GraphicRenderer::GraphicRenderer() : m_graphic_storage_list() {}

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

    void GraphicRenderer::StartScene(const Ref<Rendering::Cameras::Camera>& camera) {
        m_camera = camera;
    }

    void GraphicRenderer::Submit(const Ref<Storages::GraphicRendererStorage<float, unsigned int>>& storage) {
        const auto& material = storage->GetMaterial();
        const auto& geometry = storage->GetGeometry();
        material->Apply();

        // Todo : As used by many shader, we should moved it out to an Uniform Buffer
        const auto& shader = material->GetShader();
        shader->SetUniform("model", geometry->GetTransform());
        shader->SetUniform("view", m_camera->GetViewMatrix());
        shader->SetUniform("projection", m_camera->GetProjectionMatrix());

        const auto& vertex_array = storage->GetVertexArray();
        RendererCommand::DrawIndexed(shader, vertex_array);
    }
} // namespace ZEngine::Rendering::Renderers
