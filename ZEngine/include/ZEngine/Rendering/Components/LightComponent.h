#pragma once
#include <Rendering/Lights/Light.h>
#include <Maths/Math.h>

namespace ZEngine::Rendering::Components {
    struct LightComponent {
        LightComponent(Maths::Vector3 ambiant_color = {1.0f, 1.0f, 1.0f}, Maths::Vector3 diffuse_color = {1.0f, 1.0f, 1.0f}, Maths::Vector3 specular_color = {1.0f, 1.0f, 1.0f}) {
            m_light = CreateRef<Lights::BasicLight>();
            m_light->SetAmbientColor(ambiant_color);
            m_light->SetDiffuseColor(diffuse_color);
            m_light->SetSpecularColor(specular_color);
        }

        ~LightComponent() = default;

        Ref<Lights::BasicLight> GetLight() const {
            return m_light;
        }

    private:
        Ref<Lights::BasicLight> m_light;
    };

} // namespace ZEngine::Rendering::Components
