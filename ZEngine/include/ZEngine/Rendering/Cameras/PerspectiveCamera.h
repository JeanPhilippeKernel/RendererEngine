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

    public:
        struct Movement
        {
            bool forward_    = false;
            bool backward_   = false;
            bool left_       = false;
            bool right_      = false;
            bool up_         = false;
            bool down_       = false;
            bool mousePan    = false;
            bool mouseZoom   = false;
            bool mouseRotate = false;
        } movement_;

    public:
        float mouseSpeed_   = 0.8f;
        float acceleration_ = 2.0f;
        float damping_      = 0.2f;
        float maxSpeed_     = 0.02f;
        float fastCoef_     = 10.0f;

    protected:
        glm::quat GetOrientation();
        glm::vec3 GetForward();
        glm::vec3 GetUp();
        glm::vec3 GetRight();

    protected:
        glm::vec2 mousePos_         = glm::vec2(0);
        double    m_distance        = 10;
        float     m_yaw_angle       = 0.0f;
        float     m_pitch_angle     = 0.0f;
        float     m_viewport_width  = 1.0f;
        float     m_viewport_height = 1.0f;
    };
} // namespace ZEngine::Rendering::Cameras
