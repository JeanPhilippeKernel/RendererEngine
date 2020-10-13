#pragma once
#include <memory>
#include "../Cameras/Camera.h"
#include "../../Z_EngineDef.h"


namespace  Z_Engine::Rendering::Scenes {
	class GraphicScene
	{
	public:
		explicit GraphicScene() =  default;
	
		explicit GraphicScene(const Ref<Z_Engine::Rendering::Cameras::Camera>& camera)
			:m_camera(camera)
		{}

		~GraphicScene() = default;

		Ref<Z_Engine::Rendering::Cameras::Camera> GetCamera() const;

		void SetCamera(const Ref<Z_Engine::Rendering::Cameras::Camera>& camera);

	private:
		WeakRef<Z_Engine::Rendering::Cameras::Camera> m_camera;
	};
}
