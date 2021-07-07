#pragma once
#include <memory>
#include <queue>

#include <Rendering/Renderers/RenderCommand.h>
#include <Rendering/Renderers/Storages/GraphicRendererStorage.h>

#include "Rendering/Buffers/VertexArray.h"
#include <Rendering/Cameras/Camera.h>
#include <Rendering/Textures/Texture.h>

#include <Core/IInitializable.h>
#include <Rendering/Meshes/Mesh.h>
#include <ZEngineDef.h>

namespace ZEngine::Rendering::Renderers {
	
	class GraphicRenderer : public Core::IInitializable {
	public:
		GraphicRenderer();
		virtual ~GraphicRenderer()	= default;

		virtual void AddMesh(Meshes::Mesh& mesh);
		virtual void AddMesh(Ref<Meshes::Mesh>& mesh);
		virtual void AddMesh(std::vector<Meshes::Mesh>& meshes);
		virtual void AddMesh(std::vector<Ref<Meshes::Mesh>>& meshes);


	public:
		virtual void StartScene(const Maths::Matrix4& m_view_projection_matrix);
		virtual void EndScene();

	protected:

		void Submit(const Ref<Storages::GraphicRendererStorage<float, unsigned int>>& graphic_storage) {
			
			const auto& shader			= graphic_storage->GetShader();
			const auto& vertex_array	= graphic_storage->GetVertexArray();
			const auto& material		= graphic_storage->GetMaterial();
			shader->Bind();
			material->Apply();

			shader->SetUniform("uniform_viewprojection", m_view_projection_matrix);
			RendererCommand::DrawIndexed(shader, vertex_array);
		}
																 
	protected:
		Maths::Matrix4 															m_view_projection_matrix;
		
		std::unordered_map<unsigned int, std::vector<Rendering::Meshes::Mesh>>	m_mesh_map;
		std::queue<Ref<Storages::GraphicRendererStorage<float, unsigned int>>>	m_graphic_storage_list;
		Storages::GraphicRendererStorageType									m_storage_type;
	};
}