#pragma once
#include <Maths/Math.h>

namespace ZEngine::Rendering::Components {
    struct TransformComponent {
        TransformComponent(const Maths::Vector3& position = {0.0f, 0.0f, 0.0f}, const Maths::Vector3& scale_size = {10.0f, 10.0f, 10.0f},
            const Maths::Vector3& rotation_axis = {0.0f, 1.0f, 0.0f}, float rotation_angle = 0.0f)
            : m_position(position), m_scale_size(scale_size), m_rotation_axis(rotation_axis), m_rotation_angle(rotation_angle) {}

        ~TransformComponent() = default;

        void SetPosition(const Maths::Vector3& value) {
            m_position = value;
        }

        void SetScaleSize(const Maths::Vector3& value) {
            m_scale_size = value;
        }

        void SetRotationAxis(const Maths::Vector3& value) {
            m_rotation_axis = value;
        }

        void SetRotationAngle(float value) {
            m_rotation_angle = value;
        }

        const Maths::Vector3& GetPosition() const {
            return m_position;
        }

        const Maths::Vector3& GetScaleSize() const {
            return m_scale_size;
        }

        const Maths::Vector3& GetRotationAxis() const {
            return m_rotation_axis;
        }

        float GetRotationAngle() const {
            return m_rotation_angle;
        }

    private:
        Maths::Vector3 m_position;
        Maths::Vector3 m_rotation_axis;
        Maths::Vector3 m_scale_size;
        float          m_rotation_angle;
    };
} // namespace ZEngine::Rendering::Components
