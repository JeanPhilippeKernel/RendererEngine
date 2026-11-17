#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/SceneRuntimeCamera.h>
#include <cmath>

namespace ZEngine::ECS
{
    bool CaptureSceneRuntimeCamera(const Scene& scene, const uuids::uuid& requested_camera, uint32_t viewport_width, uint32_t viewport_height, Rendering::Cameras::CameraFrameData* output, SceneDiagnostics* diagnostics)
    {
        if (!output || !viewport_width || !viewport_height)
        {
            SceneDiagnosticError(diagnostics, "A runtime camera requires an output and a nonzero viewport.", SceneDiagnosticCode::InvalidArgument);
            return false;
        }
        const Components::CameraComponent*    camera    = nullptr;
        const Components::TransformComponent* transform = nullptr;
        EntityID                              selected  = {};
        uint32_t                              matches   = 0;
        scene.ForEachEntity([&](EntityID entity) {
            const auto* current = scene.GetComponent<Components::CameraComponent>(entity);
            const auto* id      = scene.GetComponent<Components::UUIDComponent>(entity);
            if (current && (requested_camera.is_nil() ? current->IsMain : id && id->Value == requested_camera))
            {
                ++matches;
                camera    = current;
                transform = scene.GetComponent<Components::TransformComponent>(entity);
                selected  = entity;
            }
        });
        if (matches != 1 || !transform || !camera)
        {
            SceneDiagnosticError(diagnostics, "The requested runtime camera is missing, ambiguous, or has no transform.", SceneDiagnosticCode::InvalidData);
            return false;
        }
        const auto* node   = scene.Hierarchy().Find(selected);
        const float aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
        if (!node || !node->TopologyValid || !node->FrameValid || !std::isfinite(camera->FovY) || camera->FovY <= 0.f || camera->FovY >= 180.f || !std::isfinite(camera->Near) || !std::isfinite(camera->Far) || camera->Near <= 0.f || camera->Far <= camera->Near || !std::isfinite(aspect) || aspect <= 0.f)
        {
            SceneDiagnosticError(diagnostics, "The runtime camera has an invalid hierarchy, transform, or projection.", SceneDiagnosticCode::InvalidData);
            return false;
        }
        Rendering::Cameras::CameraFrameData result = {};
        if (!Core::Maths::Geometry::TryInverse(transform->WorldTransform, &result.View))
        {
            SceneDiagnosticError(diagnostics, "The runtime camera transform is not invertible.", SceneDiagnosticCode::InvalidData);
            return false;
        }
        const float tangent                             = std::tan(Core::Maths::radians(camera->FovY) * 0.5f);
        const float n                                   = camera->Near;
        const float f                                   = camera->Far;
        result.Projection                               = Core::Maths::Mat4f(1.f / (aspect * tangent), 0.f, 0.f, 0.f, 0.f, -1.f / tangent, 0.f, 0.f, 0.f, 0.f, f / (n - f), (n * f) / (n - f), 0.f, 0.f, -1.f, 0.f);
        result.Position                                 = {transform->WorldTransform(0, 3), transform->WorldTransform(1, 3), transform->WorldTransform(2, 3)};
        result.ProjectionType                           = Rendering::Cameras::CameraType::PERSPECTIVE;
        Core::Maths::Geometry::ProjectionFrame verified = {};
        if (!Core::Maths::Geometry::MakeProjectionFrame(result.View, result.Projection, result.Position, Core::Maths::Geometry::ProjectionKind::Perspective, false, &verified))
        {
            SceneDiagnosticError(diagnostics, "The runtime camera cannot produce a finite projection.", SceneDiagnosticCode::InvalidData);
            return false;
        }
        *output = result;
        return true;
    }
} // namespace ZEngine::ECS
