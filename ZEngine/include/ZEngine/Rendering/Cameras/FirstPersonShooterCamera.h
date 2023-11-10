#pragma once
#include <Rendering/Cameras/PerspectiveCamera.h>

namespace ZEngine::Rendering::Cameras
{

    class FirstPersonShooterCamera : public PerspectiveCamera
    {
    public:
        explicit FirstPersonShooterCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw, float pitch);
        ~FirstPersonShooterCamera() = default;

        float GetRadius() const
        {
            return m_radius;
        }

        void SetYawAngle(float value);
        void SetPitchAngle(float value);

        float GetYawAngle() const
        {
            return glm::degrees(m_yaw_angle);
        }
        float GetPitchAngle() const
        {
            return glm::degrees(m_pitch_angle);
        }

        void SetTarget(const glm::vec3& target) override;
        void SetPosition(const glm::vec3& position) override;

        void SetPosition(float yaw, float pitch);
        void Move(const glm::vec3& offset);

    protected:
        virtual void UpdateCoordinateVectors() override;
    };

} // namespace ZEngine::Rendering::Cameras
