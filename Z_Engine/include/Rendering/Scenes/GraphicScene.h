#pragma once
#include <vector>

#include "../Cameras/Camera.h"
#include "../../Z_EngineDef.h"

#include "../../Core/IRenderable.h"
#include "../../Core/IInitializable.h"

#include "../../Controllers/ICameraController.h"

#include "../Meshes/Mesh.h"
#include "../Renderers/GraphicRenderer.h"

namespace  Z_Engine::Rendering::Scenes {
	class GraphicScene : 
		public Core::IInitializable, 
		public Core::IRenderable {

	public:
		explicit GraphicScene(Controllers::ICameraController* const controller) 
			: 
			m_camera_controller(nullptr),
			m_renderer(nullptr)
		{
			m_camera_controller.reset(controller);
		}

		virtual ~GraphicScene() = default;

		void Initialize() override;
		virtual void Render() override;

		void Add(Ref<Meshes::Mesh>& mesh);
		void Add(const std::vector<Ref<Meshes::Mesh>>& meshes);

		virtual const Scope<Controllers::ICameraController>& GetCameraController() const;

	protected:
		Scope<Controllers::ICameraController>	m_camera_controller;
		Scope<Renderers::GraphicRenderer>		m_renderer;

		std::vector<Ref<Meshes::Mesh>>			m_mesh_list;
	};
}
