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
        virtual ~ICameraController()                                                          = default;

        virtual Core::Maths::Vec3f            GetPosition() const                             = 0;
        virtual void                          SetPosition(const Core::Maths::Vec3f& position) = 0;
        virtual Rendering::Cameras::CameraPtr GetCamera() const                               = 0;

        CameraControllerType                  GetControllerType() const
        {
            return m_controller_type;
        }

    protected:
        CameraControllerType   m_controller_type{CameraControllerType::UNDEFINED};
        Windows::CoreWindowPtr m_window = nullptr;
    };
    ZDEFINE_PTR(ICameraController);
} // namespace ZEngine::Controllers
