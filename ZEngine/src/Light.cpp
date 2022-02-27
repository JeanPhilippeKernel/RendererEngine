#include <Rendering/Lights/Light.h>

namespace ZEngine::Rendering::Lights {

    void Light::SetAmbientColor(const Maths::Vector3& value) {
        m_ambient_color = value;
    }

    void Light::SetDiffuseColor(const Maths::Vector3& value) {
        m_diffuse_color = value;
    }

    void Light::SetSpecularColor(const Maths::Vector3& value) {
        m_specular_color = value;
    }

    Maths::Vector3 Light::GetAmbientColor() const {
        return m_ambient_color;
    }

    Maths::Vector3 Light::GetDiffuseColor() const {
        return m_diffuse_color;
    }

    Maths::Vector3 Light::GetSpecularColor() const {
        return m_specular_color;
    }
} // namespace ZEngine::Rendering::Lights
