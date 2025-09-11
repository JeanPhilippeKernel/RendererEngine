#pragma once
#include <Core/Maths/Quaternion.h>
#include <Rendering/Cameras/Camera.h>

namespace ZEngine::Rendering::Cameras
{

    class PerspectiveCamera : public Camera
    {
    public:
        PerspectiveCamera()          = default;
        virtual ~PerspectiveCamera() = default;

        virtual void                            Initialize(float field_of_view, float aspect_ratio, float clip_near, float clip_far, float yaw_rad = 0.0f, float pitch_rad = 0.0f);

        virtual void                            SetTarget(const ZEngine::Core::Maths::Vec3f& target);
        virtual void                            SetPosition(const ZEngine::Core::Maths::Vec3f& position);
        virtual void                            SetProjectionMatrix(const ZEngine::Core::Maths::Mat4f& projection);

        virtual void                            Update(float time_step, const ZEngine::Core::Maths::Vec2f& mouse_position, bool mouse_pressed);

        virtual void                            Zoom(float delta);
        virtual void                            SetDistance(double distance);
        virtual std::pair<float, float>         PanSpeed() const;
        virtual void                            SetViewport(float width, float height);

        virtual ZEngine::Core::Maths::Mat4f     GetViewMatrix() override;
        virtual ZEngine::Core::Maths::Mat4f     GetPerspectiveMatrix() const override;
        virtual ZEngine::Core::Maths::Vec3f     GetPosition() const override;

        ZEngine::Core::Maths::Quaternion<float> GetOrientation();
        ZEngine::Core::Maths::Vec3f             GetForward();
        ZEngine::Core::Maths::Vec3f             GetUp();
        ZEngine::Core::Maths::Vec3f             GetRight();

    public:
        struct Movement
        {
            bool MousePan    = false;
            bool MouseZoom   = false;
            bool MouseRotate = false;
        } Movement;

    protected:
        ZEngine::Core::Maths::Vec2f m_mouse_pos       = ZEngine::Core::Maths::Vec2f(0.0f, 0.0f);
        float                       m_mouse_speed     = 0.8f;
        float                       m_acceleration    = 2.0f;
        float                       m_damping         = 0.2f;
        float                       m_max_speed       = 0.02f;
        float                       m_fast_coef       = 10.0f;
        double                      m_distance        = 10;
        float                       m_yaw_angle       = 0.0f;
        float                       m_pitch_angle     = 0.0f;
        float                       m_viewport_width  = 1.0f;
        float                       m_viewport_height = 1.0f;
    };
} // namespace ZEngine::Rendering::Cameras
