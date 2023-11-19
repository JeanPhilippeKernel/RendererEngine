#pragma once
#include <ZEngineDef.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Rendering/Cameras/CameraEnum.h>

namespace ZEngine::Rendering::Cameras
{
    class Camera
    {
    public:
        Camera()          = default;
        virtual ~Camera() = default;

        virtual const glm::vec3& GetPosition() const
        {
            return m_position;
        }

        virtual void SetPosition(const glm::vec3& position)
        {
            m_position = position;
        }

        virtual void SetProjectionMatrix(const glm::mat4& projection)
        {
            m_projection = projection;
        }

        virtual const glm::mat4& GetViewMatrix() const
        {
            return m_view_matrix;
        }

        virtual const glm::mat4& GetProjectionMatrix() const
        {
            return m_projection;
        }

        virtual const glm::mat4& GetViewProjectionMatrix() const
        {
            return m_view_projection;
        }

        virtual const glm::vec3& GetUp()
        {
            return m_up;
        }

        virtual const glm::vec3& GetForward()
        {
            return m_forward;
        }

        virtual const glm::vec3& GetRight()
        {
            return m_right;
        }

        virtual void SetTarget(const glm::vec3& target)
        {
            m_target = target;
        }

        virtual const glm::vec3& GetTarget() const
        {
            return m_target;
        }

        virtual CameraType GetCameraType() const
        {
            return m_camera_type;
        }

    protected:
        virtual void UpdateViewMatrix() = 0;

    protected:
        float           m_field_of_view;
        float           m_aspect_ratio;
        float           m_clip_near;
        float           m_clip_far;
        glm::vec3       m_position{0.0f, 0.0f, 0.5f};
        glm::vec3       m_target{0.0f, 0.0f, 0.0f};
        glm::vec3       m_forward{0.0f, 0.0f, 1.0f};
        glm::vec3       m_up{0.0f, 0.0f, 0.0f};
        glm::vec3       m_right{0.0f, 0.0f, 0.0f};
        const glm::vec3 m_world_up{0.0f, 1.0f, 0.0f};
        glm::mat4       m_view_matrix{glm::identity<glm::mat4>()};
        glm::mat4       m_projection{glm::identity<glm::mat4>()};
        glm::mat4       m_view_projection{glm::identity<glm::mat4>()};
        CameraType      m_camera_type{CameraType::UNDEFINED};
    };
} // namespace ZEngine::Rendering::Cameras
