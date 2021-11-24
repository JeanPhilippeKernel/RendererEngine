#pragma once
#include <Rendering/Materials/ShaderMaterial.h>
#include <Rendering/Geometries/IGeometry.h>

#include <ZEngineDef.h>

namespace ZEngine::Rendering::Meshes {

    class Mesh {
    public:
        explicit Mesh();
        explicit Mesh(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::ShaderMaterial>&& material);
        explicit Mesh(Ref<Geometries::IGeometry>& geometry, Ref<Materials::ShaderMaterial>& material);
        explicit Mesh(Geometries::IGeometry* const geometry, Materials::ShaderMaterial* const material);

        virtual ~Mesh() = default;

        void SetUniqueIdentifier(unsigned int value);
        void SetMaterial(const Ref<Materials::ShaderMaterial>& material);
        void SetMaterial(Ref<Materials::ShaderMaterial>& material);
        void SetMaterial(Materials::ShaderMaterial* const material);

        void SetGeometry(const Ref<Geometries::IGeometry>& geometry);
        void SetGeometry(Ref<Geometries::IGeometry>& geometry);
        void SetGeometry(Geometries::IGeometry* const geometry);

        unsigned int                          GetUniqueIdentifier() const;
        const Ref<Materials::ShaderMaterial>& GetMaterial() const;
        const Ref<Geometries::IGeometry>&     GetGeometry() const;

    private:
        unsigned int                   m_unique_identifier;
        Ref<Materials::ShaderMaterial> m_material{nullptr};
        Ref<Geometries::IGeometry>     m_geometry{nullptr};
    };
} // namespace ZEngine::Rendering::Meshes
