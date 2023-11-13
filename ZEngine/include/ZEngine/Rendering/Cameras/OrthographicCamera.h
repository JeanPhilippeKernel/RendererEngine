#pragma once

#include <Rendering/Cameras/Camera.h>

namespace ZEngine::Rendering::Cameras {
    class OrthographicCamera : public Camera {
    public:
        OrthographicCamera(float left, float right, float bottom, float top);

        OrthographicCamera(float left, float right, float bottom, float top, float degree_angle);

        ~OrthographicCamera() = default;

        void SetRotation(float rad_angle);

        float GetRotation() const {
            return m_angle;
        }

        void SetPosition(const glm::vec3& position) override;
        void SetProjectionMatrix(const glm::mat4& projection) override;

    protected:
        void UpdateViewMatrix() override;

    private:
        float m_angle{0.0f};
    };
} // namespace ZEngine::Rendering::Cameras
