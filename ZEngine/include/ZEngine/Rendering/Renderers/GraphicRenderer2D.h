#pragma once
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine::Rendering::Renderers {

    class GraphicRenderer2D : public GraphicRenderer {
    public:
        explicit GraphicRenderer2D();
        ~GraphicRenderer2D() = default;

        void Initialize() override;
    };
} // namespace ZEngine::Rendering::Renderers
