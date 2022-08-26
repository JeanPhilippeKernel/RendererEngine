#pragma once
#include <vector>
#include <ZEngineDef.h>
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Geometries/IGeometry.h>

namespace ZEngine::Rendering::Meshes {

    class Mesh {
    public:
        explicit Mesh();
        explicit Mesh(const Ref<Geometries::IGeometry>& geometry, const std::vector<Ref<Materials::ShaderMaterial>>& materials);
        explicit Mesh(Ref<Geometries::IGeometry>&& geometry, std::vector<Ref<Materials::ShaderMaterial>>&& materials);

        virtual ~Mesh() = default;

        void SetGeometry(const Ref<Geometries::IGeometry>& geometry);
        void SetGeometry(Ref<Geometries::IGeometry>&& geometry);

        Ref<Geometries::IGeometry>                         GetGeometry() const;
        const std::vector<Ref<Materials::ShaderMaterial>>& GetMaterials() const;

    private:
        std::vector<Ref<Materials::ShaderMaterial>> m_material_collection;
        Ref<Geometries::IGeometry>                  m_geometry;
    };
} // namespace ZEngine::Rendering::Meshes
