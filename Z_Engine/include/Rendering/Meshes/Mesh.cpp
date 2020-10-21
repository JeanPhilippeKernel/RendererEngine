#include "Mesh.h"
#include "../Renderers/Storages/GraphicVertex.h"

using namespace Z_Engine::Rendering::Renderers::Storages;

namespace Z_Engine::Rendering::Meshes {

	Mesh::Mesh() 
		:
		m_unique_identifier(0),
		m_geometry(nullptr), m_material(nullptr) 
	{}

	Mesh::Mesh(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::ShaderMaterial>&& material)
		:
		m_unique_identifier(0),
		m_geometry(std::move(geometry)), 
		m_material(std::move(material))
	{
		if (m_material != nullptr)
			OnSetMaterialEvent();
	}

	Mesh::Mesh(Ref<Geometries::IGeometry>& geometry, Ref<Materials::ShaderMaterial>& material) 
		:
		m_unique_identifier(0),
		m_geometry(geometry), m_material(material)
	{
		if (m_material != nullptr)
			OnSetMaterialEvent();
	}


	unsigned int Mesh::GetIdentifier() const {
		return m_unique_identifier;
	}

	const Ref<Materials::ShaderMaterial>& Mesh::GetMaterial() const { 
		return m_material; 
	}
	
	const Ref<Geometries::IGeometry>& Mesh::GetGeometry() const { 
		return m_geometry; 
	}

	void Mesh::SetIdentifier(unsigned int  value) {
		if(m_unique_identifier != value) {
			m_unique_identifier = value;
			OnSetIdentifierEvent();
		}
	}
	void Mesh::SetMaterial(const Ref<Materials::ShaderMaterial>& material) {
		m_material = material;
		OnSetMaterialEvent();
	}

	void Mesh::SetGeometry(const Ref<Geometries::IGeometry>& geometry) {
		m_geometry = geometry;
	}
	
	void Mesh::OnSetMaterialEvent() {
		m_material->SetAttributes();
	}

	void Mesh::OnSetIdentifierEvent() {
		 auto& vertices = m_geometry->GetVertices();
		 std::for_each(
			 std::begin(vertices), std::end(vertices), 
			 [this](GraphicVertex& vertex) { vertex.SetIndex(m_unique_identifier); }
		 );
	}

}