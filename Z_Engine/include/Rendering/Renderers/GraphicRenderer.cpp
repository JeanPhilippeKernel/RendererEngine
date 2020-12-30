#include "GraphicRenderer.h"


namespace Z_Engine::Rendering::Renderers {
   GraphicRenderer::GraphicRenderer()
	   :
	   m_view_projection_matrix(glm::mat4(1.0f)),
	   m_mesh_map(),
	   m_graphic_storage_list()
   {
   }

   void GraphicRenderer::AddMesh(Meshes::Mesh& mesh) {
	   const auto& material				= mesh.GetMaterial();
	   const auto& material_hashcode	= material->GetHashCode();

	   auto it = std::find_if(
		   std::begin(m_mesh_map), std::end(m_mesh_map), 
		   [&material_hashcode](const std::pair<unsigned int, std::vector<Meshes::Mesh>>& value) {
			   return value.first == material_hashcode;
		   });
	   
	   if (it == std::end(m_mesh_map)) {
		   std::vector<Meshes::Mesh> meshes = { mesh };
		   mesh.SetUniqueIdentifier(0);
		   m_mesh_map.emplace(material_hashcode, meshes);
	   }

	   else {
		   unsigned int mesh_identifier_id = it->second.size();
		   mesh.SetUniqueIdentifier(mesh_identifier_id);
		   it->second.emplace_back(mesh);
	   }
   }

   void GraphicRenderer::AddMesh(Ref<Meshes::Mesh>& mesh) {
	   AddMesh(*mesh);
   }

   void GraphicRenderer::AddMesh(std::vector<Meshes::Mesh>& meshes) {
	   std::for_each(
		   std::begin(meshes), std::end(meshes),
		   [this](Meshes::Mesh& mesh) { this->AddMesh(mesh); }
	   );
   }

   void GraphicRenderer::AddMesh(std::vector<Ref<Meshes::Mesh>>& meshes) {
	   std::for_each(
		   std::begin(meshes), std::end(meshes),
		   [this](Ref<Meshes::Mesh>& mesh) { this->AddMesh(mesh); }
	   );
   }


   void GraphicRenderer::StartScene(const glm::mat4& view_projection_matrix) {
	   m_view_projection_matrix = view_projection_matrix;
   }

   void GraphicRenderer::EndScene() {
	   
	   std::for_each(std::begin(m_mesh_map), std::end(m_mesh_map),
		   [this](const std::pair<unsigned int, std::vector<Meshes::Mesh>>& value) {

			   const auto& material = value.second.at(0).GetMaterial();
			   const Ref<Shaders::Shader>& shader = material->GetShader();

			   std::vector<Ref<Buffers::VertexBuffer<float>>> buffers;
			   
			   std::transform(
				   std::begin(value.second), std::end(value.second),
				   std::back_inserter(buffers),
				   [](const Meshes::Mesh& mesh) { return mesh.GetGeometry()->GetBuffer(); }
			   );

			   Ref<Storages::GraphicRendererStorage<float, unsigned int>> graphic_storage;
			   graphic_storage.reset(new Storages::GraphicRendererStorage<float, unsigned int>{ shader, buffers, material });
			   m_graphic_storage_list.emplace(graphic_storage);
		   }
	   );

	   while (!m_graphic_storage_list.empty()) {
		   const auto& storage = m_graphic_storage_list.front();
		   this->Submit(storage);
		   m_graphic_storage_list.pop();
	   }

	   m_mesh_map.clear();
   }
}