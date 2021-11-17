#pragma once
#include <vector>
#include <Rendering/Cameras/Camera.h>
#include <ZEngineDef.h>
#include <Core/IRenderable.h>
#include <Core/IInitializable.h>
#include <Controllers/ICameraController.h>
#include <Rendering/Meshes/Mesh.h>
#include <Rendering/Renderers/GraphicRenderer.h>

namespace  ZEngine::Rendering::Scenes {
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
		
		void UpdateSize(uint32_t width, uint32_t);
		uint32_t ToTextureRepresentation() const;

		void Add(Ref<Meshes::Mesh>& mesh);
		void Add(const std::vector<Ref<Meshes::Mesh>>& meshes);

		virtual const Scope<Controllers::ICameraController>& GetCameraController() const;

	protected:
		Scope<Controllers::ICameraController>	m_camera_controller;
		Scope<Renderers::GraphicRenderer>		m_renderer;

		std::vector<Ref<Meshes::Mesh>>			m_mesh_list;
	};
}
