#pragma once
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine::Rendering::Renderers {

    class GraphicRenderer3D : public GraphicRenderer {
    public:
        explicit GraphicRenderer3D();
        ~GraphicRenderer3D() = default;

        void Initialize() override;
    };
} // namespace ZEngine::Rendering::Renderers
