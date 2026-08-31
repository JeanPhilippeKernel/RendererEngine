#pragma once
#include <ZEngine/Controllers/CameraControllerTypeEnums.h>
#include <ZEngine/Controllers/IController.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Windows/CoreWindow.h>

namespace ZEngine::Controllers
{

    struct ICameraController : public IController
    {
        ICameraController() {}
        virtual ~ICameraController()                                                                  = default;

        virtual Core::Maths::Vec3f            GetPosition() const                                     = 0;
        virtual void                          SetPosition(const Core::Maths::Vec3f& position)         = 0;
        virtual Rendering::Cameras::CameraPtr GetCamera() const                                       = 0;
        virtual void                          Update(Core::TimeStep dt)                               = 0;
        virtual bool                          OnEvent(Core::CoreEvent&)                               = 0;

        virtual void                          SetViewport(float logicalW, float logicalH)             = 0;
        virtual void                          SetViewportOrigin(float x, float y)                     = 0;
        /// @brief Notify the controller of the viewport's screen rect each frame.
        ///        Used for self-contained hover detection — no ZUI hit-test dependency.
        /// @param x0 Left edge in logical pixels.
        /// @param y0 Top edge in logical pixels.
        /// @param x1 Right edge in logical pixels.
        /// @param y1 Bottom edge in logical pixels.
        virtual void                          SetViewportRect(float x0, float y0, float x1, float y1) = 0;
        virtual void                          ResumeEventProcessing()                                 = 0;
        virtual void                          PauseEventProcessing()                                  = 0;

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
