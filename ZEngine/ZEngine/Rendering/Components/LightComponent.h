#pragma once
#include <Rendering/Lights/Light.h>

namespace ZEngine::Rendering::Components
{
    struct LightComponent
    {
        LightComponent(const Ref<Lights::LightVNext>& light) : m_light(light) {}
        LightComponent(Ref<Lights::LightVNext>&& light) : m_light(std::move(light)) {}

        ~LightComponent() = default;

        Ref<Lights::LightVNext> GetLight() const
        {
            return m_light;
        }

    private:
        Ref<Lights::LightVNext> m_light;
    };

} // namespace ZEngine::Rendering::Components
