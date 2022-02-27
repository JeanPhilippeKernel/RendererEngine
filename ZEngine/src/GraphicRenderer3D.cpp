#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer3D.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>

namespace ZEngine::Rendering::Renderers {

    GraphicRenderer3D::GraphicRenderer3D() : GraphicRenderer(), m_mesh_collection() {
        m_storage_type = Storages::GraphicRendererStorageType::GRAPHIC_3D_STORAGE_TYPE;

        Buffers::FrameBufferSpecification spec;
        spec.HasDepth   = true;
        spec.HasStencil = true;
        spec.Width      = 1080;
        spec.Height     = 800;

        m_framebuffer.reset(new Buffers::FrameBuffer(spec));
    }

    void GraphicRenderer3D::Initialize() {
        // enable management of transparent background image (RGBA-8)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // enable Z-depth and stencil testing
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
    }

    void GraphicRenderer3D::AddMesh(Meshes::Mesh& mesh) {
        if (mesh.IsLight()) {
            m_light_collection.push_back(mesh);
        }

        m_mesh_collection.push_back(mesh);
    }

    void GraphicRenderer3D::EndScene() {

        std::for_each(std::begin(m_mesh_collection), std::end(m_mesh_collection), [this](const Meshes::Mesh& mesh) {
            const auto&                                                geometry = mesh.GetGeometry();
            const auto&                                                material = mesh.GetMaterial();
            const auto&                                                shader   = material->GetShader();
            Ref<Storages::GraphicRendererStorage<float, unsigned int>> graphic_storage;
            graphic_storage.reset(new Storages::GraphicRendererStorage<float, unsigned int>{shader, geometry, material, m_storage_type});
            m_graphic_storage_list.emplace(std::move(graphic_storage));
        });

        while (!m_graphic_storage_list.empty()) {
            const auto& storage = m_graphic_storage_list.front();
            this->Submit(storage);
            m_graphic_storage_list.pop();
        }

        m_mesh_collection.clear();
        m_light_collection.clear();
        m_mesh_collection.shrink_to_fit();
        m_light_collection.shrink_to_fit();
    }
} // namespace ZEngine::Rendering::Renderers
