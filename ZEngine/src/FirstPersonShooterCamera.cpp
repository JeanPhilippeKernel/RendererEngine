#include <pch.h>
#include <Rendering/Cameras/FirstPersonShooterCamera.h>

namespace ZEngine::Rendering::Cameras
{

    FirstPersonShooterCamera::FirstPersonShooterCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw_rad, float pitch_rad)
        : PerspectiveCamera(field_of_view, aspect_ratio, near, far)
    {
        m_yaw_angle   = yaw_rad;
        m_pitch_angle = pitch_rad;
    }

    void FirstPersonShooterCamera::SetYawAngle(float degree)
    {
        m_yaw_angle = glm::radians(degree);
    }

    void FirstPersonShooterCamera::SetPitchAngle(float degree)
    {
        float rad     = glm::radians(degree);
        m_pitch_angle = glm::clamp(rad, -glm::pi<float>() + 0.1f, glm::pi<float>() - 0.1f);
    }

    void FirstPersonShooterCamera::SetTarget(const glm::vec3& target)
    {
        Camera::SetTarget(target);
        m_forward = glm::normalize(m_position - m_target);
        m_radius  = glm::length(m_forward);
    }

    void FirstPersonShooterCamera::SetPosition(const glm::vec3& position)
    {
        Camera::SetPosition(position);
        glm::vec3 direction = m_position - m_target;
        m_radius            = glm::length(direction);

        glm::quat quat_around_y  = glm::angleAxis(m_yaw_angle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat quat_around_x  = glm::angleAxis(m_pitch_angle, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat quat_around_yx = quat_around_y * quat_around_x;

        //direction  = glm::rotate(glm::normalize(quat_around_yx), glm::normalize(direction));
        m_position = m_target + (m_radius * direction);
        m_forward  = glm::normalize(m_position - m_target);

        UpdateCoordinateVectors();
        PerspectiveCamera::UpdateViewMatrix();
    }

    void FirstPersonShooterCamera::Move(const glm::vec3& offset)
    {

        m_position = glm::translate(glm::identity<glm::mat4>(), offset) * glm::vec4(m_position, 1.0f);

        if (m_position.y <= 0.0f)
        {
            m_position.y = 0.0f;
        }

        UpdateCoordinateVectors();
        PerspectiveCamera::UpdateViewMatrix();
    }

    void FirstPersonShooterCamera::UpdateCoordinateVectors()
    {
        m_right  = glm::cross(m_world_up, m_forward);
        m_up     = glm::cross(m_forward, m_right);
        m_target = m_position - (m_radius * m_forward);
    }

    void FirstPersonShooterCamera::SetPosition(float yaw_degree, float pitch_degree)
    {

        float yaw_radians   = glm::radians(yaw_degree);
        float pitch_radians = glm::radians(pitch_degree);

        glm::quat quat_around_y  = glm::angleAxis(yaw_radians, m_up);
        glm::quat quat_around_x  = glm::angleAxis(pitch_radians, m_right);
        glm::quat quat_around_yx = quat_around_y * quat_around_x;

        //m_forward = glm::rotate(glm::normalize(quat_around_yx), glm::normalize(m_forward));

        UpdateCoordinateVectors();
        PerspectiveCamera::UpdateViewMatrix();
    }
} // namespace ZEngine::Rendering::Cameras
