#pragma once
#include <Rendering/Meshes/Mesh.h>
#include <Maths/Math.h>

namespace ZEngine::Rendering::Lights {

    struct Light : public Meshes::Mesh {
        explicit Light() : Mesh() {
            m_is_light_mesh_object = true;
        }

        explicit Light(Ref<Geometries::IGeometry>&& geometry, Ref<Materials::ShaderMaterial>&& material) : Mesh(geometry, material) {}
        explicit Light(Ref<Geometries::IGeometry>& geometry, Ref<Materials::ShaderMaterial>& material) : Mesh(geometry, material) {}
        explicit Light(Geometries::IGeometry* const geometry, Materials::ShaderMaterial* const material) : Mesh(geometry, material) {}

        virtual ~Light() = default;

        void SetAmbientColor(const Maths::Vector3& value);
        void SetDiffuseColor(const Maths::Vector3& value);
        void SetSpecularColor(const Maths::Vector3& value);

        Maths::Vector3 GetAmbientColor() const;
        Maths::Vector3 GetDiffuseColor() const;
        Maths::Vector3 GetSpecularColor() const;

    protected:
        Maths::Vector3 m_ambient_color{1.0f};
        Maths::Vector3 m_diffuse_color{1.0f};
        Maths::Vector3 m_specular_color{1.0f};
    };
} // namespace ZEngine::Rendering::Lights
