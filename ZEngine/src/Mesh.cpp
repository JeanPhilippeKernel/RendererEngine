#include <pch.h>
#include <Rendering/Meshes/Mesh.h>

namespace ZEngine::Rendering::Meshes
{

    Mesh::Mesh(const Ref<Geometries::IGeometry>& geometry, const std::vector<Ref<Materials::ShaderMaterial>>& materials) : m_geometry(geometry), m_material_collection(materials) {}

    Mesh::Mesh(Ref<Geometries::IGeometry>&& geometry, std::vector<Ref<Materials::ShaderMaterial>>&& materials)
        : m_geometry(std::move(geometry)), m_material_collection(std::move(materials))
    {
    }

    Ref<Geometries::IGeometry> Mesh::GetGeometry() const
    {
        return m_geometry;
    }

    const std::vector<Ref<Materials::ShaderMaterial>>& Mesh::GetMaterials() const
    {
        return m_material_collection;
    }

    void Mesh::SetGeometry(Ref<Geometries::IGeometry>&& geometry)
    {
        m_geometry = std::move(geometry);
    }

    void Mesh::SetGeometry(const Ref<Geometries::IGeometry>& geometry)
    {
        m_geometry = geometry;
    }
} // namespace ZEngine::Rendering::Meshes
