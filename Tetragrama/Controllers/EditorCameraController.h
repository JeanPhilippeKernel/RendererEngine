#pragma once
#include <ZEngine/Applications/GameApplication.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Windows/CoreWindow.h>

namespace Tetragrama::Controllers
{
    struct EditorCameraController : public ZEngine::Controllers::FlyCameraController
    {
        EditorCameraController()          = default;
        virtual ~EditorCameraController() = default;

        void Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window, ZEngine::Input::InputManager* input_manager, ZEngine::Applications::GameApplication* app);
    };
    ZDEFINE_PTR(EditorCameraController);
} // namespace Tetragrama::Controllers
