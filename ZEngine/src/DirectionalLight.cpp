#include <Rendering/Lights/DirectionalLight.h>

namespace ZEngine::Rendering::Lights {
    DirectionalLight::DirectionalLight() {
        m_light_type               = LightType::DIRECTIONAL_LIGHT;
        m_directional_light_buffer = CreateScope<Buffers::UniformBuffer<Maths::Vector4>>(2);
    }

    void DirectionalLight::SetAmbientColor(const Maths::Vector3& value) {
        m_ambient_color = value;
        UpdateBuffer();
    }

    void DirectionalLight::SetDiffuseColor(const Maths::Vector3& value) {
        m_diffuse_color = value;
        UpdateBuffer();
    }

    void DirectionalLight::SetSpecularColor(const Maths::Vector3& value) {
        m_specular_color = value;
        UpdateBuffer();
    }

    Maths::Vector3 DirectionalLight::GetAmbientColor() const {
        return m_ambient_color;
    }

    Maths::Vector3 DirectionalLight::GetDiffuseColor() const {
        return m_diffuse_color;
    }

    Maths::Vector3 DirectionalLight::GetSpecularColor() const {
        return m_specular_color;
    }

    void DirectionalLight::SetDirection(const Maths::Vector3& direction) {
        m_direction = direction;
        UpdateBuffer();
    }

    const Maths::Vector3& DirectionalLight::GetDirection() const {
        return m_direction;
    }

    void DirectionalLight::UpdateBuffer() {
        m_directional_light_buffer->SetData(
            {Maths::Vector4(-m_direction, 0.0), Maths::Vector4(m_ambient_color, 1.0), Maths::Vector4(m_diffuse_color, 1.0), Maths::Vector4(m_specular_color, 1.0)});
        m_directional_light_buffer->Bind();
    }
} // namespace ZEngine::Rendering::Lights
