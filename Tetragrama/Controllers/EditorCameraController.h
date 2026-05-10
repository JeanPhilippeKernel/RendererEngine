#pragma once
#include <ZEngine/Controllers/FlyCameraController.h>

namespace Tetragrama::Controllers
{
    struct EditorCameraController : public ZEngine::Controllers::FlyCameraController
    {
        EditorCameraController()          = default;
        virtual ~EditorCameraController() = default;

        void Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window);
    };
    ZDEFINE_PTR(EditorCameraController);
} // namespace Tetragrama::Controllers
