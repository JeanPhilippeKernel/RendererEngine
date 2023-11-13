#pragma once
#include <Rendering/Cameras/Camera.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ZEngine::Rendering::Cameras
{

    class PerspectiveCamera : public Camera
    {
    public:
        PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far);
        PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far , float yaw_rad, float pitch_rad);
        virtual ~PerspectiveCamera() = default;

        virtual void SetTarget(const glm::vec3& target) override;

        virtual void SetPosition(const glm::vec3& position) override;
        virtual void SetProjectionMatrix(const glm::mat4& projection) override;

    protected:
        virtual void UpdateCoordinateVectors();
        virtual void UpdateViewMatrix() override;

    protected:
        float m_radius;
        float m_yaw_angle;
        float m_pitch_angle;
    };
} // namespace ZEngine::Rendering::Cameras
