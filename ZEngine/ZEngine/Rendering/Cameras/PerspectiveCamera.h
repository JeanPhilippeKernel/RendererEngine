#pragma once
#include <Rendering/Cameras/Camera.h>

namespace ZEngine::Rendering::Cameras
{

    class PerspectiveCamera : public Camera
    {
    public:
        PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far);
        PerspectiveCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far, float yaw_rad, float pitch_rad);
        virtual ~PerspectiveCamera() = default;

        virtual void SetTarget(const glm::vec3& target);
        virtual void SetPosition(const glm::vec3& position);
        virtual void SetProjectionMatrix(const glm::mat4& projection);

        virtual void Update(float time_step, const glm::vec2& mouse_position, bool mouse_pressed);

        virtual void                    Zoom(float delta);
        virtual void                    SetDistance(double distance);
        virtual std::pair<float, float> PanSpeed() const;
        virtual void                    SetViewport(float width, float height);

        virtual glm::mat4 GetViewMatrix() override;
        virtual glm::mat4 GetPerspectiveMatrix() const override;
        virtual glm::vec3 GetPosition() const override;

        glm::quat GetOrientation();
        glm::vec3 GetForward();
        glm::vec3 GetUp();
        glm::vec3 GetRight();

    public:
        struct Movement
        {
            bool MousePan    = false;
            bool MouseZoom   = false;
            bool MouseRotate = false;
        } Movement;

    protected:
        glm::vec2 m_mouse_pos       = glm::vec2(0);
        float     m_mouse_speed     = 0.8f;
        float     m_acceleration    = 2.0f;
        float     m_damping         = 0.2f;
        float     m_max_speed       = 0.02f;
        float     m_fast_coef       = 10.0f;
        double    m_distance        = 10;
        float     m_yaw_angle       = 0.0f;
        float     m_pitch_angle     = 0.0f;
        float     m_viewport_width  = 1.0f;
        float     m_viewport_height = 1.0f;
    };
} // namespace ZEngine::Rendering::Cameras
