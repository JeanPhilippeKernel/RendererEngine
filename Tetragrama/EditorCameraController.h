#pragma once
#include <ZEngine.h>

namespace Tetragrama
{
    struct EditorCameraController : public ZEngine::Controllers::PerspectiveCameraController
    {
        EditorCameraController(double distance, float yaw_angle_degree, float pitch_angle_degree);
        virtual ~EditorCameraController() = default;
    };
} // namespace Tetragrama
