#pragma once
#include <Rendering/Renderers/GraphicRenderer.h>

namespace ZEngine::Rendering::Renderers {

    class GraphicRenderer3D : public GraphicRenderer {
    public:
        GraphicRenderer3D();
        virtual ~GraphicRenderer3D() = default;

        void Initialize() override;

        virtual void EndScene() override;
    };
} // namespace ZEngine::Rendering::Renderers
