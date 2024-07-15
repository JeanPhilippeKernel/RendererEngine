#include <pch.h>
#include <glm/gtx/quaternion.hpp>
#include <Rendering/Cameras/PerspectiveCamera.h>

namespace ZEngine::Rendering::Cameras
{

    PerspectiveCamera::PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far)
        : PerspectiveCamera(field_of_view, aspect_ratio, clip_near, clip_far, 0, 0)
    {
    }

    PerspectiveCamera::PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far, float yaw_rad, float pitch_rad)
        : m_yaw_angle(yaw_rad), m_pitch_angle(pitch_rad)
    {
        Fov           = glm::radians(field_of_view);
        AspectRatio   = aspect_ratio;
        ClipNear      = clip_near;
        ClipFar       = clip_far;
        Target        = {0.f, 0.f, 0.f};
        Type          = CameraType::PERSPECTIVE;
    }

    void PerspectiveCamera::SetTarget(const glm::vec3& target)
    {
        Target = target;
    }

    void PerspectiveCamera::SetPosition(const glm::vec3& position)
    {
        Position = position;
    }

    void PerspectiveCamera::SetProjectionMatrix(const glm::mat4& projection)
    {
        Projection = projection;
    }

    void PerspectiveCamera::Update(float time_step, const glm::vec2& mouse_position, bool mouse_pressed)
    {
        const glm::vec2 delta = (mouse_position - m_mouse_pos) * 0.003f;
        if (mouse_pressed)
        {
            if (Movement.MousePan)
            {
                auto [xSpeed, ySpeed] = PanSpeed();
                Target += -GetRight() * delta.x * xSpeed * static_cast<float>(m_distance);
                Target += GetUp() * delta.y * ySpeed * static_cast<float>(m_distance);
            }
            if (Movement.MouseRotate)
            {
                float yawSign = GetUp().y < 0 ? -1.0f : 1.0f;
                m_yaw_angle += yawSign * m_mouse_speed * delta.x;
                m_pitch_angle += m_mouse_speed * delta.y;
            }

            if (Movement.MouseZoom)
            {
                Zoom(delta.y *0.1f);
            }
        }
        m_mouse_pos = mouse_position;
    }

    void PerspectiveCamera::Zoom(float delta)
    {
        float distance = m_distance * 0.2f;
        distance       = std::max(distance, 0.0f);
        float speed    = distance * distance;
        speed          = std::min(speed, 100.0f);

        m_distance -= delta * speed;
        m_distance = glm::clamp(static_cast<float>(m_distance), 3.0f, ClipFar);
    }

    void PerspectiveCamera::SetDistance(double distance)
    {
        m_distance = distance;
    }

    std::pair<float, float> PerspectiveCamera::PanSpeed() const
    {
        float x       = std::min(m_viewport_width / 1000.0f, 4.4f); // max = 2.4f
        float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

        float y       = std::min(m_viewport_height / 1000.0f, 4.4f); // max = 2.4f
        float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

        return {xFactor, yFactor};
    }

    void PerspectiveCamera::SetViewport(float width, float height)
    {
        AspectRatio            = width / height;
        m_viewport_width       = width;
        m_viewport_height      = height;
    }

    glm::mat4 PerspectiveCamera::GetViewMatrix()
    {
        Position              = Target - GetForward() * static_cast<float>(m_distance);
        auto      orientation = GetOrientation();
        glm::mat4 view        = glm::translate(glm::mat4(1.0f), Position) * glm::mat4_cast(orientation);
        view                  = glm::inverse(view);
        return view;
    }

    glm::mat4 PerspectiveCamera::GetPerspectiveMatrix() const
    {
        /*
        * Ref : https://johannesugb.github.io/gpu-programming/why-do-opengl-proj-matrices-fail-in-vulkan/
        * Unlike the article, for our implementation we decided to use have the y-axis Up.
        * For future Gfx API we may want to revisit/adapt it.
        */
        glm::mat4 I = glm::identity<glm::mat4>();
        I[2][2]     = -1;

        float inv_a          = m_viewport_height / m_viewport_width;
        float tan_half_fov   = glm::tan(Fov / 2);
        float far_minus_near = ClipFar - ClipNear;

        glm::mat4 P(0);
        P[0][0] = (inv_a / tan_half_fov);
        P[1][1] = (1.0f / tan_half_fov);
        P[2][2] = (ClipFar / far_minus_near);
        P[2][3] = 1;
        P[3][2] = (-(ClipFar * ClipNear) / far_minus_near);

        return P * I;
    }

    glm::vec3 PerspectiveCamera::GetPosition() const
    {
        return Position;
    }

    glm::quat PerspectiveCamera::GetOrientation()
    {
        return glm::quat(glm::vec3(-m_pitch_angle, -m_yaw_angle, 0.0f));
    }

    glm::vec3 PerspectiveCamera::GetForward()
    {
        return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
    }

    glm::vec3 PerspectiveCamera::GetUp()
    {
        return glm::rotate(GetOrientation(), WorldUp);
    }

    glm::vec3 PerspectiveCamera::GetRight()
    {
        return glm::rotate(GetOrientation(), glm::vec3(1.0f, 0.0f, 0.0f));
    }
} // namespace ZEngine::Rendering::Cameras
