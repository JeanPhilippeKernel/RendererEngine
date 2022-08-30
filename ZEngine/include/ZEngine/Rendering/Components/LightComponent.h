#pragma once
#include <Rendering/Lights/Light.h>
#include <Maths/Math.h>

namespace ZEngine::Rendering::Components {
    struct LightComponent {
        LightComponent(const Ref<Lights::Light>& light) : m_light(light) {}
        LightComponent(Ref<Lights::Light>&& light) : m_light(std::move(light)) {}

        ~LightComponent() = default;

        Ref<Lights::Light> GetLight() const {
            return m_light;
        }

    private:
        Ref<Lights::Light> m_light;
    };

} // namespace ZEngine::Rendering::Components
