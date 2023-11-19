#pragma once
#include <ZEngine.h>

namespace Tetragrama
{
    enum MoveDirection
    {
        FORWARD = 0,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };
    class EditorCamera : public ZEngine::Rendering::Cameras::PerspectiveCamera
    {
    public:
        explicit EditorCamera(float field_of_view, float aspect_ratio, float clip_near, float clip_far, float yaw_rad, float pitch_rad);

        float GetDistance() const;
        void  SetDistance(float distance);

        const glm::vec3& GetUp() override;
        const glm::vec3& GetRight() override;
        const glm::vec3& GetForward() override;

        void SetViewPortSize(float width, float height);

        void UpdateViewMatrix() override;

        void MousePan(const glm::vec2& delta);
        void MouseRotate(const glm::vec2& delta);
        void MouseZoom(float delta);
        void Move(MoveDirection direction, float delta);

        std::pair<float, float> PanSpeed() const;
        float                   RotationSpeed() const;
        float                   ZoomSpeed() const;

    private:
        float                   m_movement_speed = .02f;
        float                   m_distance      = 10.0f;
        glm::quat               m_orientation   = {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3               m_focal_point   = {0.0f, 0.0f, 0.0f};
        std::pair<float, float> m_viewport_size = {0.0f, 0.0f};
    };
} // namespace Tetragrama
