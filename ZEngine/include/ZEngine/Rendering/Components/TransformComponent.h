#pragma once
#include <Maths/Math.h>

namespace ZEngine::Rendering::Components
{
    struct TransformComponent
    {
        TransformComponent()  = default;
        ~TransformComponent() = default;
        TransformComponent(glm::mat4& transform) : m_transform(transform) {}

        void SetPosition(const Maths::Vector3& value)
        {
            m_position = value;
        }

        void SetScaleSize(const Maths::Vector3& value)
        {
            m_scale_size = value;
        }

        void SetRotation(const Maths::Vector3& rad_angles)
        {
            m_rotation = rad_angles;
        }

        void SetRotationEulerAngles(const Maths::Vector3& euler_angles)
        {
            m_rotation = Maths::radians(euler_angles);
        }

        const Maths::Vector3& GetPosition() const
        {
            return m_position;
        }

        const Maths::Vector3& GetScaleSize() const
        {
            return m_scale_size;
        }

        const Maths::Vector3& GetRotation() const
        {
            return m_rotation;
        }

        Maths::Vector3 GetRotationEulerAngles() const
        {
            return Maths::degrees(m_rotation);
        }

        const Maths::mat4& GetTransform()
        {
            return m_transform;
        }

    private:
        Maths::Vector3 m_position;
        Maths::Vector3 m_rotation;
        Maths::Vector3 m_scale_size;
        glm::mat4      m_transform;
    };
} // namespace ZEngine::Rendering::Components
