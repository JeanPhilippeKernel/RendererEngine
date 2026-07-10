#pragma once
#include <ZEngine/Core/Maths/Matrix.h>

namespace ZEngine::Rendering::Components
{
    struct TransformComponent
    {
        TransformComponent()  = default;
        ~TransformComponent() = default;
        TransformComponent(ZEngine::Core::Maths::Mat4f& transform) : m_transform(transform) {}

        void SetPosition(const ZEngine::Core::Maths::Vec3f& value)
        {
            m_position = value;
        }

        void SetScaleSize(const ZEngine::Core::Maths::Vec3f& value)
        {
            m_scale_size = value;
        }

        void SetRotation(const ZEngine::Core::Maths::Vec3f& rad_angles)
        {
            m_rotation = rad_angles;
        }

        void SetRotationEulerAngles(const ZEngine::Core::Maths::Vec3f& euler_angles)
        {
            m_rotation = ZEngine::Core::Maths::radians(euler_angles);
        }

        const ZEngine::Core::Maths::Vec3f& GetPosition() const
        {
            return m_position;
        }

        const ZEngine::Core::Maths::Vec3f& GetScaleSize() const
        {
            return m_scale_size;
        }

        const ZEngine::Core::Maths::Vec3f& GetRotation() const
        {
            return m_rotation;
        }

        ZEngine::Core::Maths::Vec3f GetRotationEulerAngles() const
        {
            return ZEngine::Core::Maths::degrees(m_rotation);
        }

        const ZEngine::Core::Maths::Mat4f& GetTransform()
        {
            return m_transform;
        }

    private:
        ZEngine::Core::Maths::Vec3f m_position;
        ZEngine::Core::Maths::Vec3f m_rotation;
        ZEngine::Core::Maths::Vec3f m_scale_size;
        ZEngine::Core::Maths::Mat4f m_transform;
    };
} // namespace ZEngine::Rendering::Components
