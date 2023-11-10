#include <pch.h>
#include "EditorCamera.h"

using namespace ZEngine::Rendering::Cameras;

namespace Tetragrama
{

    EditorCamera::EditorCamera(float field_of_view, float aspect_ratio, float clipping_near, float clipping_far, float yaw_rad, float pitch_rad)
        : PerspectiveCamera(field_of_view, aspect_ratio, clipping_near, clipping_far, yaw_rad, pitch_rad)
    {
        UpdateViewMatrix();
    }

    void EditorCamera::UpdateViewMatrix()
    {
        glm::quat orientation = glm::quat(glm::vec3(-m_pitch_angle, -m_yaw_angle, 0.0f));
        glm::vec3 forward     = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
        glm::vec3 right       = glm::rotate(orientation, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 up          = glm::rotate(orientation, glm::vec3(0.0f, 1.0f, 0.0f));

        m_position    = m_focal_point - forward * m_distance;
        m_view_matrix = glm::lookAt(m_position, m_focal_point, up);

        // m_position        = (m_focal_point - m_forward * m_distance);
        // m_view_matrix     = glm::inverse(glm::translate(glm::mat4(1.0f), m_position) * glm::mat4_cast(m_orientation) * glm::translate(glm::mat4(1.0f), -m_focal_point));
        m_view_projection = m_projection * m_view_matrix;
    }

    std::pair<float, float> EditorCamera::PanSpeed() const
    {
        float x       = std::min(m_viewport_size.first / 1000.0f, 2.4f); // max = 2.4f
        float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

        float y       = std::min(m_viewport_size.second / 1000.0f, 2.4f); // max = 2.4f
        float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

        return {xFactor, yFactor};
    }

    float EditorCamera::RotationSpeed() const
    {
        return 0.8f;
    }

    float EditorCamera::ZoomSpeed() const
    {
        float distance = m_distance * 0.2f;
        distance       = std::max(distance, 0.0f);
        float speed    = distance * distance;
        speed          = std::min(speed, 100.0f);
        return speed;
    }

    void EditorCamera::MousePan(const glm::vec2& delta)
    {
        auto [xSpeed, ySpeed] = PanSpeed();
        m_focal_point += -GetRight() * delta.x * xSpeed * m_distance;
        m_focal_point += GetUp() * delta.y * ySpeed * m_distance;
    }

    void EditorCamera::MouseRotate(const glm::vec2& delta)
    {
        float yaw_sign = GetUp().y < 0 ? -1.0f : 1.0f;
        m_yaw_angle += yaw_sign * delta.x * RotationSpeed();
        m_pitch_angle += delta.y * RotationSpeed();

        UpdateViewMatrix();
        // m_orientation = glm::normalize(glm::quat(glm::vec3(-m_pitch_angle, -m_yaw_angle, 0.0f)));
    }

    void EditorCamera::MouseZoom(float delta)
    {
        m_distance -= delta * ZoomSpeed();
        if (m_distance < 1.0f)
        {
            m_distance = 1.0f;
        }

        UpdateViewMatrix();
    }

    void EditorCamera::Move(MoveDirection direction, float delta_time)
    {
        float speed = m_movement_speed * m_distance * delta_time;
        if (direction == MoveDirection::FORWARD)
        {
            m_position += GetForward() * speed;
        }

        else if (direction == MoveDirection::BACKWARD)
        {
            m_position -= GetForward() * speed;
        }

        else if (direction == MoveDirection::LEFT)
        {
            m_position -= GetRight() * speed;
        }

        else if (direction == MoveDirection::RIGHT)
        {
            m_position += GetRight() * speed;
        }

        UpdateViewMatrix();
    }

    float EditorCamera::GetDistance() const
    {
        return m_distance;
    }

    void EditorCamera::SetDistance(float distance)
    {
        m_distance = distance;
    }

    const glm::vec3& EditorCamera::GetUp()
    {
        m_up =  glm::rotate(glm::quat(glm::vec3(-m_pitch_angle, -m_yaw_angle, 0.0f)), m_world_up);
        return m_up;
        //m_up = glm::rotate(m_orientation, m_world_up);
        //return m_up;
    }

    const glm::vec3& EditorCamera::GetRight()
    {
        m_right = glm::rotate(glm::quat(glm::vec3(-m_pitch_angle, -m_yaw_angle, 0.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
        return m_right;
        //m_right = glm::rotate(m_orientation, glm::vec3(1.0f, 0.0f, 0.0f));
        //return m_right;
    }

    const glm::vec3& EditorCamera::GetForward()
    {
        m_forward = glm::rotate(glm::quat(glm::vec3(-m_pitch_angle, -m_yaw_angle, 0.0f)), glm::vec3(0.0f, 0.0f, -1.0f));
        return m_forward;
        //m_forward = glm::rotate(m_orientation, glm::vec3(0.0f, 0.0f, -1.0f));
        //return m_forward;
    }

    void EditorCamera::SetViewPortSize(float width, float height)
    {
        m_viewport_size.first  = width;
        m_viewport_size.second = height;
        m_aspect_ratio         = width / height;
        m_projection           = glm::perspective(m_field_of_view, m_aspect_ratio, m_clip_near, m_clip_far);
        UpdateViewMatrix();
    }
} // namespace Tetragrama
