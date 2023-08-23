#pragma once

#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Controllers/PerspectiveCameraController.h>

namespace ZEngine::Rendering::Scenes
{

    class GraphicScene3D : public GraphicScene
    {
    public:
        GraphicScene3D() : GraphicScene()
        {
            //m_renderer = CreateScope<Renderers::GraphicRenderer>();
        }

        ~GraphicScene3D() = default;
    };
} // namespace ZEngine::Rendering::Scenes
