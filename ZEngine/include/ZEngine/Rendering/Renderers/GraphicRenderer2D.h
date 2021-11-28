#pragma once
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine::Rendering::Renderers {

    class GraphicRenderer2D : public GraphicRenderer {
    public:
        explicit GraphicRenderer2D();
        virtual ~GraphicRenderer2D() = default;

        void Initialize() override;

        void AddMesh(Meshes::Mesh& mesh) override;

        virtual void EndScene() override;

    private:
        std::unordered_map<unsigned int, std::vector<Rendering::Meshes::Mesh>> m_mesh_map;
    };
} // namespace ZEngine::Rendering::Renderers
