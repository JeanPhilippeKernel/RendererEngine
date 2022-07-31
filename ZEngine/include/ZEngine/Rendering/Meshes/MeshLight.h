#pragma once
#include <Rendering/Meshes/Mesh.h>
#include <Maths/Math.h>
#include <Rendering/Lights/Light.h>

namespace ZEngine::Rendering::Mesh {

    struct MeshLight : public Meshes::Mesh {
        explicit MeshLight() : Mesh() {
            m_is_MeshLight_mesh_object = true;
        }

        explicit MeshLight(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::ShaderMaterial>&& material) : Mesh(geometry, material) {}
        explicit MeshLight(Ref<Geometries::IGeometry>& geometry, Ref<Materials::ShaderMaterial>& material) : Mesh(geometry, material) {}
        explicit MeshLight(Geometries::IGeometry* const geometry, Materials::ShaderMaterial* const material) : Mesh(geometry, material) {}

        virtual ~MeshLight() = default;

        void                SetLight(const Lights::BasicLight&);
        Lights::BasicLight& GetLight();

    private:
        Lights::BasicLight m_basic_light;
    };
} // namespace ZEngine::Rendering::Mesh
