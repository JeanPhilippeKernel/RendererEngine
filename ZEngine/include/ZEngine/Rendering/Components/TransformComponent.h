#pragma once
#include <Maths/Math.h>

namespace ZEngine::Rendering::Components {
    struct TransformComponent {
        TransformComponent(
            const Maths::Vector3& position = {0.0f, 0.0f, 0.0f}, const Maths::Vector3& scale_size = {10.0f, 10.0f, 10.0f}, const Maths::Vector3& rotation = {0.0f, 0.0f, 0.0f})
            : m_position(position), m_scale_size(scale_size), m_rotation(rotation), m_transform(Maths::Matrix4(1.0f)) {}

        ~TransformComponent() = default;

        void SetPosition(const Maths::Vector3& value) {
            m_position = value;
        }

        void SetScaleSize(const Maths::Vector3& value) {
            m_scale_size = value;
        }

        void SetRotation(const Maths::Vector3& rad_angles) {
            m_rotation = rad_angles;
        }

        void SetRotationEulerAngles(const Maths::Vector3& euler_angles) {
            m_rotation = Maths::radians(euler_angles);
        }

        const Maths::Vector3& GetPosition() const {
            return m_position;
        }

        const Maths::Vector3& GetScaleSize() const {
            return m_scale_size;
        }

        const Maths::Vector3& GetRotation() const {
            return m_rotation;
        }

        const Maths::Vector3& GetRotationEulerAngles() const {
            return Maths::degrees(m_rotation);
        }

        const Maths::mat4& GetTransform() {
            auto rotation_mat = Maths::toMat4(Maths::quat(m_rotation));
            m_transform       = translate(Maths::Matrix4(1.0f), m_position) * rotation_mat * scale(Maths::Matrix4(1.0f), m_scale_size);
            return m_transform;
        }

    private:
        Maths::Vector3 m_position;
        Maths::Vector3 m_rotation;
        Maths::Vector3 m_scale_size;
        Maths::mat4    m_transform;
    };
} // namespace ZEngine::Rendering::Components
