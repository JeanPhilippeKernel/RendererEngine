#include <pch.h>
#include "EditorCamera.h"

using namespace ZEngine::Rendering::Cameras;

namespace Tetragrama {

    EditorCamera::EditorCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw_rad, float pitch_rad)
        : PerspectiveCamera(field_of_view, aspect_ratio, near, far) {
        m_yaw_angle   = yaw_rad;
        m_pitch_angle = pitch_rad;
        UpdateViewMatrix();
    }

    void EditorCamera::UpdateViewMatrix() {
        m_position = CalculatePosition();

        glm::quat orientation = GetOrientation();
        m_view_matrix         = glm::translate(glm::mat4(1.0f), m_position) * glm::toMat4(orientation);
        m_view_matrix         = glm::inverse(m_view_matrix);
    }

    std::pair<float, float> EditorCamera::PanSpeed() const {
        float x       = std::min(m_viewport_size.first / 1000.0f, 2.4f); // max = 2.4f
        float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

        float y       = std::min(m_viewport_size.second / 1000.0f, 2.4f); // max = 2.4f
        float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

        return {xFactor, yFactor};
    }

    float EditorCamera::RotationSpeed() const {
        return 0.8f;
    }

    float EditorCamera::ZoomSpeed() const {
        float distance = m_distance * 0.2f;
        distance       = std::max(distance, 0.0f);
        float speed    = distance * distance;
        speed          = std::min(speed, 100.0f); // max speed = 100
        return speed;
    }

    void EditorCamera::MousePan(const glm::vec2& delta) {
        auto [xSpeed, ySpeed] = PanSpeed();
        m_focal_point += -GetRight() * delta.x * xSpeed * m_distance;
        m_focal_point += GetUp() * delta.y * ySpeed * m_distance;
    }

    void EditorCamera::MouseRotate(const glm::vec2& delta) {
        float yawSign = GetUp().y < 0 ? -1.0f : 1.0f;
        m_yaw_angle += yawSign * delta.x * RotationSpeed();
        m_pitch_angle += delta.y * RotationSpeed();
    }

    void EditorCamera::MouseZoom(float delta) {
        m_distance -= delta * ZoomSpeed();
        if (m_distance < 1.0f) {
            m_focal_point += GetForward();
            m_distance = 1.0f;
        }
    }

    float EditorCamera::GetDistance() const {
        return m_distance;
    }

    void EditorCamera::SetDistance(float distance) {
        m_distance = distance;
    }

    const glm::vec3& EditorCamera::GetUp() {
        m_up = glm::rotate(GetOrientation(), m_world_up);
        return m_up;
    }

    const glm::vec3& EditorCamera::GetRight() {
        return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    const glm::vec3& EditorCamera::GetForward() {
        m_forward = glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
        return m_forward;
    }

    glm::vec3 EditorCamera::CalculatePosition() {
        return m_focal_point - GetForward() * m_distance;
    }

    glm::quat EditorCamera::GetOrientation() const {
        return glm::quat(glm::vec3(-m_pitch_angle, -m_yaw_angle, 0.0f));
    }

    void EditorCamera::SetViewPortSize(float width, float height) {
        m_viewport_size.first  = width;
        m_viewport_size.second = height;
    }
} // namespace Tetragrama
