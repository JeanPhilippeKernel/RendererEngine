#pragma once
#include <memory>
#include "../Cameras/Camera.h"
#include "../../Z_EngineDef.h"


namespace  Z_Engine::Rendering::Renderer {
	class Z_ENGINE_API GraphicScene
	{
	public:
		explicit GraphicScene() =  default;
	
		explicit GraphicScene(const std::shared_ptr<Z_Engine::Rendering::Cameras::Camera>& camera)
			:m_camera(camera)
		{}

		~GraphicScene() = default;

		std::shared_ptr<Z_Engine::Rendering::Cameras::Camera> GetCamera() const;

		void SetCamera(const std::shared_ptr<Z_Engine::Rendering::Cameras::Camera>& camera);

	private:
		std::weak_ptr<Z_Engine::Rendering::Cameras::Camera> m_camera;
	};
}
