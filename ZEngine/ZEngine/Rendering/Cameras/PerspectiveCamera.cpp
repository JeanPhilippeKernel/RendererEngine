#include <pch.h>
#include <Core/Maths/MathUtils.h>
#include <Rendering/Cameras/PerspectiveCamera.h>

namespace ZEngine::Rendering::Cameras
{
    void PerspectiveCamera::Initialize(float field_of_view, float aspect_ratio, float clip_near, float clip_far, float yaw_rad, float pitch_rad)
    {
        Fov           = ZEngine::Core::Maths::radians(field_of_view);
        AspectRatio   = aspect_ratio;
        ClipNear      = clip_near;
        ClipFar       = clip_far;
        Target        = {0.f, 0.f, 0.f};
        Type          = CameraType::PERSPECTIVE;
        m_yaw_angle   = yaw_rad;
        m_pitch_angle = pitch_rad;
    }

    void PerspectiveCamera::SetTarget(const ZEngine::Core::Maths::Vec3f& target)
    {
        Target = target;
    }

    void PerspectiveCamera::SetPosition(const ZEngine::Core::Maths::Vec3f& position)
    {
        Position = position;
    }

    void PerspectiveCamera::SetProjectionMatrix(const ZEngine::Core::Maths::Mat4f& projection)
    {
        Projection = projection;
    }

    void PerspectiveCamera::Update(float time_step, const ZEngine::Core::Maths::Vec2f& mouse_position, bool mouse_pressed)
    {
        const ZEngine::Core::Maths::Vec2f delta = (mouse_position - m_mouse_pos) * 0.003f;
        if (mouse_pressed)
        {
            if (Movement.MousePan)
            {
                auto [xSpeed, ySpeed]  = PanSpeed();
                Target                += -GetRight() * delta.x * xSpeed * static_cast<float>(m_distance);
                Target                += GetUp() * delta.y * ySpeed * static_cast<float>(m_distance);
            }
            if (Movement.MouseRotate)
            {
                float yawSign  = GetUp().y < 0 ? -1.0f : 1.0f;
                m_yaw_angle   += yawSign * m_mouse_speed * delta.x;
                m_pitch_angle += m_mouse_speed * delta.y;
            }

            if (Movement.MouseZoom)
            {
                Zoom(delta.y * 0.1f);
            }
        }
        m_mouse_pos = mouse_position;
    }

    void PerspectiveCamera::Zoom(float delta)
    {
        float distance  = m_distance * 0.2f;
        distance        = std::max(distance, 0.0f);
        float speed     = distance * distance;
        speed           = std::min(speed, 100.0f);

        m_distance     -= delta * speed;
        m_distance      = ZEngine::Core::Maths::clamp(static_cast<float>(m_distance), 3.0f, ClipFar);
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
        AspectRatio       = width / height;
        m_viewport_width  = width;
        m_viewport_height = height;
    }

    ZEngine::Core::Maths::Mat4f PerspectiveCamera::GetViewMatrix()
    {
        Position                                = Target - GetForward() * static_cast<float>(m_distance);
        auto                        orientation = GetOrientation();
        ZEngine::Core::Maths::Mat4f view        = ZEngine::Core::Maths::translate(ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>(), Position) * ZEngine::Core::Maths::quaternionToMat4(orientation);
        view                                    = view.Inverse();
        return view;
    }

    ZEngine::Core::Maths::Mat4f PerspectiveCamera::GetPerspectiveMatrix() const
    {
        /*
         * Ref : https://johannesugb.github.io/gpu-programming/why-do-opengl-proj-matrices-fail-in-vulkan/
         * Unlike the article, for our implementation we decided to use have the y-axis Up.
         * For future Gfx API we may want to revisit/adapt it.
         */
        ZEngine::Core::Maths::Mat4f I              = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        I[2][2]                                    = -1;

        float                       inv_a          = m_viewport_height / m_viewport_width;
        float                       tan_half_fov   = ZEngine::Core::Maths::tan(Fov / 2);
        float                       far_minus_near = ClipFar - ClipNear;

        ZEngine::Core::Maths::Mat4f P(ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>());
        P[0][0] = (inv_a / tan_half_fov);
        P[1][1] = (1.0f / tan_half_fov);
        P[2][2] = (ClipFar / far_minus_near);
        P[2][3] = 1;
        P[3][2] = (-(ClipFar * ClipNear) / far_minus_near);

        return P * I;
    }

    ZEngine::Core::Maths::Vec3f PerspectiveCamera::GetPosition() const
    {
        return Position;
    }

    ZEngine::Core::Maths::Quaternion<float> PerspectiveCamera::GetOrientation()
    {
        return ZEngine::Core::Maths::fromEulerAngles(-m_pitch_angle, -m_yaw_angle, 0.0f);
    }

    ZEngine::Core::Maths::Vec3f PerspectiveCamera::GetForward()
    {
        return ZEngine::Core::Maths::rotate(GetOrientation(), ZEngine::Core::Maths::Vec3f(0.0f, 0.0f, -1.0f));
    }

    ZEngine::Core::Maths::Vec3f PerspectiveCamera::GetUp()
    {
        return ZEngine::Core::Maths::rotate(GetOrientation(), WorldUp);
    }

    ZEngine::Core::Maths::Vec3f PerspectiveCamera::GetRight()
    {
        return ZEngine::Core::Maths::rotate(GetOrientation(), ZEngine::Core::Maths::Vec3f(1.0f, 0.0f, 0.0f));
    }
} // namespace ZEngine::Rendering::Cameras
