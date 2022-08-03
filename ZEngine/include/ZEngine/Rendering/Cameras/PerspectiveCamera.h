#pragma once
#include <Rendering/Cameras/Camera.h>
#include <glm/glm/gtc/matrix_transform.hpp>

namespace ZEngine::Rendering::Cameras {

    class PerspectiveCamera : public Camera {
    public:
        PerspectiveCamera(float field_of_view, float aspect_ratio, float near, float far);
        virtual ~PerspectiveCamera() = default;

        virtual void SetTarget(const Maths::Vector3& target) override;

        virtual void SetPosition(const Maths::Vector3& position) override;
        virtual void SetProjectionMatrix(const Maths::Matrix4& projection) override;

    protected:
        virtual void UpdateCoordinateVectors();
        virtual void UpdateViewMatrix() override;

    protected:
        float m_radius;
        float m_yaw_angle;
        float m_pitch_angle;
    };
} // namespace ZEngine::Rendering::Cameras
