#include <EditorCameraController.h>
#include <ZEngine/Core/Maths/MathUtils.h>

using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Helpers;
using namespace ZEngine::Core::Maths;

namespace Tetragrama::Controllers
{
    void EditorCameraController::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window)
    {
        m_window              = window;
        m_controller_type     = ZEngine::Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;

        const auto& win_props = m_window->GetWindowProperty();
        m_camera              = ZPushStructCtorArgs(arena, FlyCamera, win_props.AspectRatio);
    }
} // namespace Tetragrama::Controllers
