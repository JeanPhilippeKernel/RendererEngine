#pragma once

#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Renderers/GraphicRenderer3D.h>
#include <Controllers/PerspectiveCameraController.h>

namespace ZEngine::Rendering::Scenes {

    class GraphicScene3D : public GraphicScene {
    public:
        explicit GraphicScene3D(Controllers::PerspectiveCameraController* const controller) : GraphicScene(controller) {
            m_renderer.reset(new Renderers::GraphicRenderer3D());
        }

        ~GraphicScene3D() = default;
    };
} // namespace ZEngine::Rendering::Scenes
