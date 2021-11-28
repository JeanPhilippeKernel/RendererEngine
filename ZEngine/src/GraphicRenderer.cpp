#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/RenderCommand.h>

namespace ZEngine::Rendering::Renderers {
    GraphicRenderer::GraphicRenderer() : m_view_projection_matrix(Maths::identity<Maths::Matrix4>()), m_graphic_storage_list() {}

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

    void GraphicRenderer::StartScene(const Maths::Matrix4& view_projection_matrix) {
        m_view_projection_matrix = view_projection_matrix;
    }

    void GraphicRenderer::Submit(const Ref<Storages::GraphicRendererStorage<float, unsigned int>>& graphic_storage) {

        const auto& shader       = graphic_storage->GetShader();
        const auto& vertex_array = graphic_storage->GetVertexArray();
        const auto& material     = graphic_storage->GetMaterial();
        shader->Bind();
        material->Apply();

        //Todo : As used by many shader, we should moved it out to an Uniform Buffer
        shader->SetUniform("uniform_viewprojection", m_view_projection_matrix);
        RendererCommand::DrawIndexed(shader, vertex_array);
    }
} // namespace ZEngine::Rendering::Renderers
