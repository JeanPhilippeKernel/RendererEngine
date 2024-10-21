#include <Controllers/ICameraController.h>
#include <ZEngine/Engine.h>

namespace Tetragrama::Controllers
{
    ICameraController::ICameraController() : m_window(ZEngine::Engine::GetWindow()) {}
    ICameraController::ICameraController(float aspect_ratio, bool can_rotate) : m_aspect_ratio(aspect_ratio), m_can_rotate(can_rotate), m_window(ZEngine::Engine::GetWindow()) {}
} // namespace Tetragrama::Controllers