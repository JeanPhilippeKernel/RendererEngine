#pragma once
#include <Controllers/CameraControllerTypeEnums.h>
#include <Controllers/IController.h>
#include <Rendering/Cameras/Camera.h>
#include <Windows/CoreWindow.h>

namespace ZEngine::Controllers
{

    struct ICameraController : public IController
    {
        ICameraController() {}
        virtual ~ICameraController()                                               = default;

        virtual Core::Maths::Vec3f GetPosition() const                             = 0;
        virtual void               SetPosition(const Core::Maths::Vec3f& position) = 0;
        virtual ZRawPtr(Rendering::Cameras::Camera) GetCamera() const              = 0;
        virtual void UpdateProjectionMatrix()                                      = 0;

        float        GetRotationAngle() const
        {
            return m_rotation_angle;
        }

        float GetZoomFactor() const
        {
            return m_zoom_factor;
        }

        float GetMoveSpeed() const
        {
            return m_move_speed;
        }

        float GetRotationSpeed() const
        {
            return m_rotation_speed;
        }

        float GetAspectRatio() const
        {
            return m_aspect_ratio;
        }

        CameraControllerType GetControllerType() const
        {
            return m_controller_type;
        }

        void SetRotationAngle(float angle)
        {
            m_rotation_angle = angle;
        }

        void SetZoomFactor(float factor)
        {
            m_zoom_factor = factor;
        }

        void SetMoveSpeed(float speed)
        {
            m_move_speed = speed;
        }

        void SetRotationSpeed(float speed)
        {
            m_rotation_speed = speed;
        }

        void SetAspectRatio(float ar)
        {
            m_aspect_ratio = ar;
            UpdateProjectionMatrix();
        }

    protected:
        Core::Maths::Vec3f   m_position{0.0f, 0.0f, 10.0f};
        float                m_rotation_angle{0.0f};
        float                m_zoom_factor{1.0f};
        float                m_move_speed{0.05f};
        float                m_rotation_speed{0.05f};
        float                m_aspect_ratio{0.0f};
        bool                 m_can_rotate{false};
        CameraControllerType m_controller_type{CameraControllerType::UNDEFINED};
        ZRawPtr(Windows::CoreWindow) m_window = nullptr;
    };
    ZDEFINE_PTR(ICameraController);
} // namespace ZEngine::Controllers
