#pragma once

#include "GraphicScene.h"
#include "../Renderers/GraphicRenderer3D.h"
#include "../../Controllers/PerspectiveCameraController.h"

namespace Z_Engine::Rendering::Scenes {

	class GraphicScene3D : public GraphicScene {
	public:
		explicit GraphicScene3D(Controllers::PerspectiveCameraController* const controller)
			: GraphicScene(controller)
		{
			m_renderer.reset(new Renderers::GraphicRenderer3D());
		}

		~GraphicScene3D() = default;


		void Render() override;
	};
}