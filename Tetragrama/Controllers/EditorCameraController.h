#pragma once
#include <ZEngine/Controllers/PerspectiveCameraController.h>

namespace Tetragrama::Controllers
{
    struct EditorCameraController : public ZEngine::Controllers::PerspectiveCameraController
    {
        EditorCameraController()          = default;
        virtual ~EditorCameraController() = default;

        void Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window, double distance, float yaw_degree, float pitch_degree);
    };
    ZDEFINE_PTR(EditorCameraController);
} // namespace Tetragrama::Controllers
