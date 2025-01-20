#pragma once
#include <PerspectiveCameraController.h>

namespace Tetragrama::Controllers
{
    struct EditorCameraController : public Controllers::PerspectiveCameraController
    {
        EditorCameraController(const ZEngine::Helpers::Ref<ZEngine::Windows::CoreWindow>& window, double distance, float yaw_angle_degree, float pitch_angle_degree);
        virtual ~EditorCameraController() = default;
    };
} // namespace Tetragrama::Controllers
