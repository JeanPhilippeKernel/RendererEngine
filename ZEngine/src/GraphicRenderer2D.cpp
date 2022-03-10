#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer2D.h>

namespace ZEngine::Rendering::Renderers {

    GraphicRenderer2D::GraphicRenderer2D() : GraphicRenderer(), m_mesh_map() {
        m_renderer_information->GraphicStorageType = Storages::GraphicRendererStorageType::GRAPHIC_2D_STORAGE_TYPE;
    }

    void GraphicRenderer2D::Initialize() {
        // enable management of transparent background image (RGBA-8)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void GraphicRenderer2D::AddMesh(Meshes::Mesh& mesh) {
        const auto& material          = mesh.GetMaterial();
        const auto& material_hashcode = material->GetHashCode();

        auto it = std::find_if(std::begin(m_mesh_map), std::end(m_mesh_map),
            [&material_hashcode](const std::pair<unsigned int, std::vector<Meshes::Mesh>>& value) { return value.first == material_hashcode; });

        if (it == std::end(m_mesh_map)) {
            std::vector<Meshes::Mesh> meshes = {mesh};
            mesh.SetUniqueIdentifier(0);
            m_mesh_map.emplace(material_hashcode, meshes);
        }

        else {
            unsigned int mesh_identifier_id = it->second.size();
            mesh.SetUniqueIdentifier(mesh_identifier_id);
            it->second.push_back(mesh);
        }
    }

    void GraphicRenderer2D::EndScene() {

        /*std::for_each(std::begin(m_mesh_map), std::end(m_mesh_map), [this](const std::pair<unsigned int, std::vector<Meshes::Mesh>>& value) {
            const auto& material = value.second.at(0).GetMaterial();
            const auto& shader   = material->GetShader();

            std::vector<Storages::GraphicVertex> vertices;
            std::for_each(std::begin(value.second), std::end(value.second), [&vertices](const Meshes::Mesh& mesh) {
                const auto& mesh_vertices = mesh.GetGeometry()->GetVertices();
                std::copy(std::begin(mesh_vertices), std::end(mesh_vertices), std::back_inserter(vertices));
            });

            Ref<Storages::GraphicRendererStorage<float, unsigned int>> graphic_storage;
            graphic_storage.reset(new Storages::GraphicRendererStorage<float, unsigned int>{shader, vertices, material, m_storage_type});
            m_graphic_storage_list.emplace(graphic_storage);
        });

        while (!m_graphic_storage_list.empty()) {
            const auto& storage = m_graphic_storage_list.front();
            this->Submit(storage);
            m_graphic_storage_list.pop();
        }

        m_mesh_map.clear();*/
    }
} // namespace ZEngine::Rendering::Renderers
