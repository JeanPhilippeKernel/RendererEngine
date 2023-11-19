#include <Controllers/ICameraController.h>
#include <Engine.h>

namespace ZEngine::Controllers
{
    ICameraController::ICameraController() : m_window(Engine::GetWindow()) {}
    ICameraController::ICameraController(float aspect_ratio, bool can_rotate) : m_aspect_ratio(aspect_ratio), m_can_rotate(can_rotate), m_window(Engine::GetWindow()) {}
} // namespace ZEngine::Controllers