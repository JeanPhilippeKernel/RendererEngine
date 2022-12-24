#pragma once
#include <Rendering/Lights/Light.h>
#include <Maths/Math.h>
// #include <Rendering/Buffers/UniformBuffer.h>

namespace ZEngine::Rendering::Lights {

    class DirectionalLight : public Light {
    public:
        DirectionalLight();
        virtual ~DirectionalLight() = default;

        void SetAmbientColor(const Maths::Vector3& value);
        void SetDiffuseColor(const Maths::Vector3& value);
        void SetSpecularColor(const Maths::Vector3& value);

        Maths::Vector3 GetAmbientColor() const;
        Maths::Vector3 GetDiffuseColor() const;
        Maths::Vector3 GetSpecularColor() const;

        void                  SetDirection(const Maths::Vector3& direction);
        const Maths::Vector3& GetDirection() const;

        virtual void UpdateBuffer() override;

    private:
        Maths::Vector3 m_direction{1.0f};
        Maths::Vector3 m_ambient_color{1.0f};
        Maths::Vector3 m_diffuse_color{1.0f};
        Maths::Vector3 m_specular_color{1.0f};
        // Scope<Buffers::UniformBuffer<Maths::Vector4>> m_directional_light_buffer;
    };
} // namespace ZEngine::Rendering::Lights
