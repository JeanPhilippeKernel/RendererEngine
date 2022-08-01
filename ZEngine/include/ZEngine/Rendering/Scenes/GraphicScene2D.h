#pragma once

#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Renderers/GraphicRenderer2D.h>
#include <Controllers/OrthographicCameraController.h>

namespace ZEngine::Rendering::Scenes {

    class GraphicScene2D : public GraphicScene {
    public:
        explicit GraphicScene2D() {
            m_renderer.reset(new Renderers::GraphicRenderer2D());
        }

        ~GraphicScene2D() = default;

        void Render() override;
    };
} // namespace ZEngine::Rendering::Scenes
