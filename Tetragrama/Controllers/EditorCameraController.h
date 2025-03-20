#pragma once
#include <PerspectiveCameraController.h>

namespace Tetragrama::Controllers
{
    struct EditorCameraController : public Controllers::PerspectiveCameraController
    {
        EditorCameraController()          = default;
        virtual ~EditorCameraController() = default;

        void Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window, double distance, float yaw_degree, float pitch_degree);
    };
} // namespace Tetragrama::Controllers
