#pragma once
#include <Maths/Math.h>
#include <Rendering/Lights/Light.h>
#include <Rendering/Meshes/Mesh.h>

namespace ZEngine::Rendering::Mesh
{

    struct MeshLight : public Meshes::Mesh
    {
        explicit MeshLight() : Mesh()
        {
            m_is_MeshLight_mesh_object = true;
        }

        explicit MeshLight(Helpers::Ref<Geometries::IGeometry>&& geometry, Helpers::Ref<Materials::ShaderMaterial>&& material) : Mesh(geometry, material) {}
        explicit MeshLight(Helpers::Ref<Geometries::IGeometry>& geometry, Helpers::Ref<Materials::ShaderMaterial>& material) : Mesh(geometry, material) {}
        explicit MeshLight(Geometries::IGeometry* const geometry, Materials::ShaderMaterial* const material) : Mesh(geometry, material) {}

        virtual ~MeshLight() = default;

        void                SetLight(const Lights::BasicLight&);
        Lights::BasicLight& GetLight();

    private:
        Lights::BasicLight m_basic_light;
    };
} // namespace ZEngine::Rendering::Mesh
