#pragma once
#include <Rendering/Lights/LightEnums.h>

namespace ZEngine::Rendering::Lights {

    struct Light {
        Light()          = default;
        virtual ~Light() = default;

        virtual void UpdateBuffer() = 0;

        LightType GetLightType() const {
            return m_light_type;
        }

    protected:
        LightType m_light_type;
    };
} // namespace ZEngine::Rendering::Lights
