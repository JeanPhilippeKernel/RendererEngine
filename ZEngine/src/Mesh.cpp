#include <pch.h>
#include <Rendering/Meshes/Mesh.h>

namespace ZEngine::Rendering::Meshes {

	Mesh::Mesh() 
		:
		m_unique_identifier(0),
		m_geometry(nullptr), m_material(nullptr) 
	{}

	Mesh::Mesh(Geometries::IGeometry* const geometry, Materials::ShaderMaterial* const material)
		:
		m_unique_identifier(0),
		m_geometry(geometry), 
		m_material(material)
	{
	}

	Mesh::Mesh(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::ShaderMaterial>&& material)
		:
		m_unique_identifier(0),
		m_geometry(std::move(geometry)), 
		m_material(std::move(material))
	{
	}

	Mesh::Mesh(Ref<Geometries::IGeometry>& geometry, Ref<Materials::ShaderMaterial>& material) 
		:
		m_unique_identifier(0),
		m_geometry(std::move(geometry)), m_material(std::move(material))
	{
	}

	unsigned int Mesh::GetUniqueIdentifier() const {
		return m_unique_identifier;
	}

	const Ref<Materials::ShaderMaterial>& Mesh::GetMaterial() const { 
		return m_material; 
	}
	
	const Ref<Geometries::IGeometry>& Mesh::GetGeometry() const { 
		return m_geometry; 
	}

	void Mesh::SetUniqueIdentifier(unsigned int  value) {
		if(m_unique_identifier != value) {
			m_unique_identifier = value;
		}
	}

	void Mesh::SetMaterial(Materials::ShaderMaterial* const material) {
		m_material.reset(material);
	}

	void Mesh::SetMaterial(Ref<Materials::ShaderMaterial>& material) {
		m_material = std::move(material);
	}

	void Mesh::SetMaterial(const Ref<Materials::ShaderMaterial>& material) {
		m_material = material;
	}
	
	void Mesh::SetGeometry(Geometries::IGeometry* const geometry) {
		m_geometry.reset(geometry);
	}

	void Mesh::SetGeometry(Ref<Geometries::IGeometry>& geometry) {
		m_geometry = std::move(geometry);
	}

	void Mesh::SetGeometry(const Ref<Geometries::IGeometry>& geometry) {
		m_geometry = geometry;
	}
}