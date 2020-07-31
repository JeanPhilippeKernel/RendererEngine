#include "GraphicScene.h"

using namespace Z_Engine::Rendering::Cameras;


namespace Z_Engine::Rendering::Renderer {

	std::shared_ptr<Camera> GraphicScene::GetCamera() const {
		return m_camera.lock();
	}

	void GraphicScene::SetCamera(const std::shared_ptr<Camera>& camera) {
		m_camera = camera;
	}
}
