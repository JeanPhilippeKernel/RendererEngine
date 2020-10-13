#include "Mesh.h"
#include "../Renderers/Storages/GraphicVertex.h"

using namespace Z_Engine::Rendering::Renderers::Storages;

namespace Z_Engine::Rendering::Meshes {

	Mesh::Mesh() : m_geometry(nullptr), m_material(nullptr) {}

	Mesh::Mesh(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::IMaterial>&& material)
		:m_geometry(std::move(geometry)), m_material(std::move(material))
	{
		if (m_material != nullptr)
			OnSetMaterialEvent();
	}

	Mesh::Mesh(Ref<Geometries::IGeometry>& geometry, Ref<Materials::IMaterial>& material) 
		:m_geometry(geometry), m_material(material)
	{
		if (m_material != nullptr)
			OnSetMaterialEvent();
	}



	const Ref<Materials::IMaterial>& Mesh::GetMaterial() const { 
		return m_material; 
	}
	
	const Ref<Geometries::IGeometry>& Mesh::GetGeometry() const { 
		return m_geometry; 
	}


	void Mesh::SetMaterial(const Ref<Materials::IMaterial>& material) {
		m_material = m_material;
		OnSetMaterialEvent();
	}

	void Mesh::SetGeometry(const Ref<Geometries::IGeometry>& geometry) {
		m_geometry = geometry;
	}
	
	void Mesh::OnSetMaterialEvent() {
		   auto& vertices = m_geometry->GetVertices();
		   std::for_each(std::begin(vertices), std::end(vertices), [this]( GraphicVertex& vertex) {
			   vertex.SetTextureTilingFactor(m_material->GetTextureTilingFactor());
			   vertex.SetTextureTintColor(m_material->GetTextureTintColor());
			   });
	}

}