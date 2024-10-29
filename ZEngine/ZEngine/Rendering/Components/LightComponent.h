#pragma once
#include <Rendering/Lights/Light.h>

namespace ZEngine::Rendering::Components
{
    struct LightComponent
    {
        LightComponent(const Helpers::Ref<Lights::LightVNext>& light) : m_light(light) {}
        LightComponent(Helpers::Ref<Lights::LightVNext>&& light) : m_light(std::move(light)) {}

        ~LightComponent() = default;

        Helpers::Ref<Lights::LightVNext> GetLight() const
        {
            return m_light;
        }

    private:
        Helpers::Ref<Lights::LightVNext> m_light;
    };

} // namespace ZEngine::Rendering::Components
