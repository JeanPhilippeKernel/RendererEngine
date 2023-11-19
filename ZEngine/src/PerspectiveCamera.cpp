#include <pch.h>
#include <Rendering/Cameras/PerspectiveCamera.h>

namespace ZEngine::Rendering::Cameras
{

    PerspectiveCamera::PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far) : m_yaw_angle(0.0f), m_pitch_angle(0.0f), m_radius(0.0f)
    {
        m_field_of_view = field_of_view;
        m_aspect_ratio  = aspect_ratio;
        m_clip_near     = clip_near;
        m_clip_far      = clip_far;
        m_camera_type   = CameraType::PERSPECTIVE;
        m_projection    = glm::perspective(m_field_of_view, m_aspect_ratio, m_clip_near, m_clip_far);
        UpdateCoordinateVectors();
        UpdateViewMatrix();
    }

    PerspectiveCamera::PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far, float yaw_rad, float pitch_rad)
        : m_yaw_angle{yaw_rad}, m_pitch_angle{pitch_rad}
    {
        m_field_of_view = field_of_view;
        m_aspect_ratio  = aspect_ratio;
        m_clip_near     = clip_near;
        m_clip_far      = clip_far;
        m_camera_type   = CameraType::PERSPECTIVE;
        m_projection    = glm::perspective(m_field_of_view, m_aspect_ratio, m_clip_near, m_clip_far);
        UpdateCoordinateVectors();
        UpdateViewMatrix();
    }

    void PerspectiveCamera::SetTarget(const glm::vec3& target)
    {
        Camera::SetTarget(target);
        UpdateCoordinateVectors();
        UpdateViewMatrix();
    }

    void PerspectiveCamera::SetPosition(const glm::vec3& position)
    {
        Camera::SetPosition(position);
        UpdateCoordinateVectors();
        UpdateViewMatrix();
    }

    void PerspectiveCamera::SetProjectionMatrix(const glm::mat4& projection)
    {
        Camera::SetProjectionMatrix(projection);
        UpdateCoordinateVectors();
        UpdateViewMatrix();
    }

    void PerspectiveCamera::UpdateCoordinateVectors()
    {
        // Calculate the new front, right, and up vectors based on yaw and pitch
        m_forward = glm::normalize(m_position - m_target);
        m_right   = glm::normalize(glm::cross(m_world_up, m_forward));
        m_up      = glm::cross(m_forward, m_right);
    }

    void PerspectiveCamera::UpdateViewMatrix()
    {
        m_view_matrix     = glm::lookAt(m_position, m_target, m_up);
        m_view_projection = m_projection * m_view_matrix;
    }
} // namespace ZEngine::Rendering::Cameras
