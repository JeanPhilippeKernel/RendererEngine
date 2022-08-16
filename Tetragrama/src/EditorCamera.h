#pragma once
#include <ZEngine.h>

namespace Tetragrama {

    //Based on @TheCherno implementation. See : https:// github.com/TheCherno/Hazel/blob/master/Hazel/src/Hazel/Renderer/EditorCamera.h

    class EditorCamera : public ZEngine::Rendering::Cameras::PerspectiveCamera {
    public:
        explicit EditorCamera(float field_of_view, float aspect_ratio, float near, float far, float yaw, float pitch);

        float GetDistance() const;
        void  SetDistance(float distance);

        const glm::vec3& GetUp() override;
        const glm::vec3& GetRight() override;
        const glm::vec3& GetForward() override;

        glm::quat GetOrientation() const;

        void SetViewPortSize(float width, float height);

        void UpdateViewMatrix() override;

        void MousePan(const glm::vec2& delta);
        void MouseRotate(const glm::vec2& delta);
        void MouseZoom(float delta);

        glm::vec3 CalculatePosition();

        std::pair<float, float> PanSpeed() const;
        float                   RotationSpeed() const;
        float                   ZoomSpeed() const;

    private:
        float                   m_distance      = 10.0f;
        glm::vec3               m_focal_point   = {0.0f, 0.0f, 0.0f};
        std::pair<float, float> m_viewport_size = {0.0f, 0.0f};
    };
} // namespace Tetragrama
