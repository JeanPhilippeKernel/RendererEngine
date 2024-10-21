#pragma once

namespace Tetragrama::Controllers
{
    enum class CameraControllerType
    {
        PERSPECTIVE_CONTROLLER = 0,
        PERSPECTIVE_ORBIT_CONTROLLER,
        PERSPECTIVE_FPS_CONTROLLER,
        ORTHOGRAPHIC_CONTROLLER,
        UNDEFINED
    };
} // namespace Tetragrama::Controllers
