#pragma once
#include <Maths/Math.h>

namespace ZEngine::Rendering::Lights {

    struct BasicLight {
        BasicLight()          = default;
        virtual ~BasicLight() = default;

        void SetAmbientColor(const Maths::Vector3& value);
        void SetDiffuseColor(const Maths::Vector3& value);
        void SetSpecularColor(const Maths::Vector3& value);

        Maths::Vector3 GetAmbientColor() const;
        Maths::Vector3 GetDiffuseColor() const;
        Maths::Vector3 GetSpecularColor() const;

        void                  SetPosition(const Maths::Vector3& position);
        const Maths::Vector3& GetPosition() const;

    protected:
        Maths::Vector3 m_position{0.0f};
        Maths::Vector3 m_ambient_color{1.0f};
        Maths::Vector3 m_diffuse_color{1.0f};
        Maths::Vector3 m_specular_color{1.0f};
    };
} // namespace ZEngine::Rendering::Lights
