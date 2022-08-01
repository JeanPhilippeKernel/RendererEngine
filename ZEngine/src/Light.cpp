#include <Rendering/Lights/Light.h>

namespace ZEngine::Rendering::Lights {

    void BasicLight::SetAmbientColor(const Maths::Vector3& value) {
        m_ambient_color = value;
    }

    void BasicLight::SetDiffuseColor(const Maths::Vector3& value) {
        m_diffuse_color = value;
    }

    void BasicLight::SetSpecularColor(const Maths::Vector3& value) {
        m_specular_color = value;
    }

    Maths::Vector3 BasicLight::GetAmbientColor() const {
        return m_ambient_color;
    }

    Maths::Vector3 BasicLight::GetDiffuseColor() const {
        return m_diffuse_color;
    }

    Maths::Vector3 BasicLight::GetSpecularColor() const {
        return m_specular_color;
    }

    void BasicLight::SetPosition(const Maths::Vector3& position) {
        m_position = position;
    }

    const Maths::Vector3& BasicLight::GetPosition() const {
        return m_position;
    }
} // namespace ZEngine::Rendering::Lights
