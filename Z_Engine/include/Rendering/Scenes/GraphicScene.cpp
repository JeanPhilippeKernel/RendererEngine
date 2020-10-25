#include "GraphicScene.h"

using namespace Z_Engine::Rendering::Cameras;


namespace  Z_Engine::Rendering::Scenes {

	Ref<Camera> GraphicScene::GetCamera() const {
		return m_camera.lock();
	}

	void GraphicScene::SetCamera(const Ref<Camera>& camera) {
		m_camera = camera;
	}
}
