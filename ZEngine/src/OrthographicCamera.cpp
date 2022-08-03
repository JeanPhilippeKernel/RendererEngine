#include <pch.h>
#include <Rendering/Cameras/OrthographicCamera.h>

namespace ZEngine::Rendering::Cameras {

    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top) {
        m_camera_type = CameraType::ORTHOGRAPHIC;
        m_projection  = Maths::ortho(left, right, bottom, top, -1.0f, 1.0f);
        UpdateViewMatrix();
    }

    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top, float degree_angle) {
        m_camera_type = CameraType::ORTHOGRAPHIC;
        m_projection  = Maths::ortho(left, right, bottom, top, -1.0f, 1.0f);
        m_angle       = Maths::radians(degree_angle);
        UpdateViewMatrix();
    }

    void OrthographicCamera::SetRotation(float radian_angle) {
        m_angle = radian_angle;
        UpdateViewMatrix();
    }

    void OrthographicCamera::SetPosition(const Maths::Vector3& position) {
        Camera::SetPosition(position);
        UpdateViewMatrix();
    }

    void OrthographicCamera::SetProjectionMatrix(const Maths::Matrix4& projection) {
        Camera::SetProjectionMatrix(projection);
        UpdateViewMatrix();
    }

    void OrthographicCamera::UpdateViewMatrix() {
        const auto transform = Maths::translate(Maths::Matrix4(1.0f), m_position) * Maths::rotate(Maths::Matrix4(1.0f), m_angle, Maths::Vector3(0.f, 0.f, 1.f));

        // we default use left-handed coordinate system
        // inversing operation is to switch right-handed coordinate system
        // m_view_matrix = (m_position.z > 0) ? Maths::inverse(transform) : transform;

        m_view_matrix     = Maths::inverse(transform);
        m_view_projection = m_projection * m_view_matrix;
    }
} // namespace ZEngine::Rendering::Cameras
