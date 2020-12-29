#pragma once
#include <memory>
#include <queue>

#include "RenderCommand.h"
#include "Storages/GraphicRendererStorage.h"

#include "../Scenes/GraphicScene.h"		
#include "../Buffers/VertexArray.h"
#include "../Cameras/Camera.h"
#include "../Textures/Texture.h"

#include "../../Core/IInitializable.h"
#include "../Meshes/Mesh.h"
#include "../../Z_EngineDef.h"


namespace Z_Engine::Rendering::Renderers {
	
	class GraphicRenderer : public Core::IInitializable {
	public:
		GraphicRenderer();
		~GraphicRenderer()	= default;

		void Initialize() override {
			//enable management of transparent background image (RGBA-8)
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}


		virtual void AddMesh(Meshes::Mesh& mesh);
		virtual void AddMesh(Ref<Meshes::Mesh>& mesh);
		virtual void AddMesh(std::vector<Meshes::Mesh>& meshes);
		virtual void AddMesh(std::vector<Ref<Meshes::Mesh>>& meshes);


	public:
		virtual void StartScene(const Ref<Cameras::Camera>& camera); 
		virtual void EndScene();

	protected:

		void Submit(const Ref<Storages::GraphicRendererStorage<float, unsigned int>>& graphic_storage) {
			
			const auto& shader = graphic_storage->GetShader();
			const auto& vertex_array = graphic_storage->GetVertexArray();
			const auto& material = graphic_storage->GetMaterial();
			shader->Bind();
			material->Apply();

			shader->SetUniform("uniform_viewprojection", m_scene->GetCamera()->GetViewProjectionMatrix());
			RendererCommand::DrawIndexed(shader, vertex_array);
		}
																 
	protected:
		Ref<Scenes::GraphicScene>												m_scene;
		std::unordered_map<unsigned int, std::vector<Rendering::Meshes::Mesh>>	m_mesh_map;
		std::queue<Ref<Storages::GraphicRendererStorage<float, unsigned int>>>	m_graphic_storage_list;
	};
}