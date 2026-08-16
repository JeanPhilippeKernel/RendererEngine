#include <Tetragrama/Controllers/EditorCameraController.h>
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Core::Maths;

namespace Tetragrama::Controllers
{
    void EditorCameraController::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Windows::CoreWindow* window, ZEngine::Input::InputManager* input_manager, ZEngine::Applications::GameApplication* app)
    {
        m_window                     = window;
        m_controller_type            = ZEngine::Controllers::CameraControllerType::PERSPECTIVE_CONTROLLER;

        const auto&   props          = window->GetWindowProperty();
        float         logicalW       = static_cast<float>(props.Width);
        float         logicalH       = static_cast<float>(props.Height);

        CameraSetting settings       = {};
        settings.FOV                 = 60.0f;
        settings.NearPlane           = 0.1f;
        settings.FarPlane            = 10000.0f;
        settings.SmoothingFactor     = 12.0f;
        settings.MinMoveSpeed        = 1.0f;
        settings.MaxMoveSpeed        = 500.0f;
        settings.PanSpeed            = 1.0f;
        settings.RotationSpeed       = 0.25f;
        settings.OrbitSpeed          = 0.25f;
        settings.FastSpeedMultiplier = 4.0f;
        settings.ScrollSpeed         = 0.5f;
        settings.FocusDuration       = 0.25f;
        settings.MinOrbitDistance    = 0.5f;
        settings.MaxOrbitDistance    = 10000.0f;

        m_camera                     = ZPushStructCtorArgs(arena, FlyCamera, logicalW / logicalH, settings);
        m_camera->SetViewportSize(logicalW, logicalH);

        m_camera->Hooks.Raycast            = [](Vec3f, Vec3f, float maxDist) { return maxDist; };

        m_camera->Hooks.GetSelectionBounds = [app]() -> std::pair<Vec3f, float> {
            using namespace ZEngine::Rendering::Scenes;
            static const std::pair<Vec3f, float> kFallback = {
                Vec3f{0.0f, 0.0f, 0.0f},
                5.0f
            };

            if (!app || !app->CurrentScene)
                return kFallback;

            auto*   scene  = app->CurrentScene;
            int32_t sel_id = scene->SelectedInstanceId.value.load(std::memory_order_acquire);
            if (sel_id <= 0)
                return kFallback;

            // Seqlock read — abort if writer is active; this is best-effort for a focus op.
            uint64_t seq1 = scene->m_seq.value.load(std::memory_order_acquire);
            if (seq1 & 1)
                return kFallback;

            for (uint32_t i = 0; i < scene->Instances.size(); ++i)
            {
                if ((int32_t) scene->Instances[i].Id == sel_id)
                {
                    const auto& t      = scene->Instances[i].Transform;
                    Vec3f       center = {t[3][0], t[3][1], t[3][2]};

                    uint64_t    seq2   = scene->m_seq.value.load(std::memory_order_acquire);
                    if (seq1 != seq2)
                        return kFallback;

                    return {center, 2.0f};
                }
            }
            return kFallback;
        };

        FlyCameraController::Initialize(input_manager, arena);
    }
} // namespace Tetragrama::Controllers
