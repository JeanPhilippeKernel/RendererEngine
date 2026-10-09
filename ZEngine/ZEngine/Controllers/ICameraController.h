#pragma once
#include <ZEngine/Controllers/CameraControllerTypeEnums.h>
#include <ZEngine/Controllers/IController.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Windows/CoreWindow.h>
#include <cmath>

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
        /// @param x0 Left edge in GLFW window coordinates.
        /// @param y0 Top edge in GLFW window coordinates.
        /// @param x1 Right edge in GLFW window coordinates.
        /// @param y1 Bottom edge in GLFW window coordinates.
        virtual void                          SetViewportRect(float x0, float y0, float x1, float y1) = 0;
        /// @brief Converts UI bounds and extent to the same window coordinates as cursor input.
        /// @param input_scale UI coordinates per GLFW window coordinate, independently per axis.
        void                                  SetViewportFromUI(const float rect[4], const float input_scale[2])
        {
            const float sx = std::isfinite(input_scale[0]) && input_scale[0] > 0.f ? input_scale[0] : 1.f;
            const float sy = std::isfinite(input_scale[1]) && input_scale[1] > 0.f ? input_scale[1] : 1.f;
            const float x0 = rect[0] / sx;
            const float y0 = rect[1] / sy;
            const float x1 = rect[2] / sx;
            const float y1 = rect[3] / sy;
            SetViewportRect(x0, y0, x1, y1);
            if (x1 > x0 && y1 > y0)
                SetViewport(x1 - x0, y1 - y0);
        }
        /// @brief Inform the controller that the UI owns one or both input channels.
        /// @param pointer_captured True while a non-viewport UI control owns pointer input.
        /// @param keyboard_captured True while a UI control, popup, or modal owns keyboard input.
        virtual void         SetInputCapture(bool pointer_captured, bool keyboard_captured) {}
        virtual void         ResumeEventProcessing() = 0;
        virtual void         PauseEventProcessing()  = 0;

        CameraControllerType GetControllerType() const
        {
            return m_controller_type;
        }

    protected:
        CameraControllerType   m_controller_type{CameraControllerType::UNDEFINED};
        Windows::CoreWindowPtr m_window = nullptr;
    };
    ZDEFINE_PTR(ICameraController);
} // namespace ZEngine::Controllers
