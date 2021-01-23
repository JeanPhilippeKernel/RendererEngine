#pragma once

#include "GraphicScene.h"
#include "../Renderers/GraphicRenderer2D.h"
#include "../../Controllers/OrthographicCameraController.h"

namespace Z_Engine::Rendering::Scenes {

	class GraphicScene2D : public GraphicScene {
	public:
		explicit GraphicScene2D(Controllers::OrthographicCameraController* const controller)
			: GraphicScene(controller)
		{
			m_renderer.reset(new Renderers::GraphicRenderer2D());
		}

		~GraphicScene2D() = default;

		void Render() override;
	};
}