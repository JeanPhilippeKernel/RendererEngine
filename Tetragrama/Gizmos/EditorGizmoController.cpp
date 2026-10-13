#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Gizmos/EditorGizmoController.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/Tools/GizmoMath.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/Rendering/Scenes/SceneRayQuery.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <cmath>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::ECS::Tools;
using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Rendering::Renderers;

namespace Tetragrama::Gizmos
{
    namespace
    {
        constexpr float    GizmoPixels         = 112.f;
        constexpr float    AxisHitPixels       = 9.f;
        constexpr float    PlaneHitPixels      = 16.f;
        constexpr float    RingHitPixels       = 10.f;
        constexpr float    UniformHitPixels    = 11.f;
        constexpr float    TranslationSnap     = 0.5f;
        constexpr float    RotationSnapRadians = 0.2617993878f; // 15 degrees
        constexpr float    ScaleSnap           = 0.1f;
        constexpr uint64_t GizmoOwnerKey       = 0xA00F'4B71'2D3C'11E5ULL;

        [[nodiscard]] bool IsFinite(const Vec3f& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool TryNormalize(const Vec3f& input, Vec3f& output)
        {
            const float length = input.magnitude();
            if (!IsFinite(input) || !std::isfinite(length) || length <= 1.0e-5f)
                return false;
            output = input / length;
            return true;
        }

        [[nodiscard]] Vec3f MatrixColumn(const Mat4f& matrix, uint32_t column)
        {
            return {matrix(0, column), matrix(1, column), matrix(2, column)};
        }

        [[nodiscard]] Vec3f MatrixTranslation(const Mat4f& matrix)
        {
            return {matrix(0, 3), matrix(1, 3), matrix(2, 3)};
        }

        [[nodiscard]] Mat4f MakeBasis(Vec3f pivot, Vec3f x, Vec3f y, Vec3f z)
        {
            Mat4f       result  = Identity<Mat4f>();
            const Vec3f axes[3] = {x, y, z};
            for (uint32_t column = 0; column < 3; ++column)
            {
                result(0, column) = axes[column].x;
                result(1, column) = axes[column].y;
                result(2, column) = axes[column].z;
            }
            result(0, 3) = pivot.x;
            result(1, 3) = pivot.y;
            result(2, 3) = pivot.z;
            return result;
        }

        [[nodiscard]] Mat4f MakeTranslation(Vec3f translation)
        {
            Mat4f result = Identity<Mat4f>();
            result(0, 3) = translation.x;
            result(1, 3) = translation.y;
            result(2, 3) = translation.z;
            return result;
        }

        [[nodiscard]] Mat4f MakeScale(Vec3f scale)
        {
            Mat4f result = Identity<Mat4f>();
            result(0, 0) = scale.x;
            result(1, 1) = scale.y;
            result(2, 2) = scale.z;
            return result;
        }

        [[nodiscard]] Mat4f MakeAxisRotation(Vec3f input_axis, float radians)
        {
            Vec3f axis = {};
            if (!TryNormalize(input_axis, axis) || !std::isfinite(radians))
                return Identity<Mat4f>();
            const float c      = std::cos(radians);
            const float s      = std::sin(radians);
            const float t      = 1.f - c;
            Mat4f       result = Identity<Mat4f>();
            result(0, 0)       = t * axis.x * axis.x + c;
            result(0, 1)       = t * axis.x * axis.y - s * axis.z;
            result(0, 2)       = t * axis.x * axis.z + s * axis.y;
            result(1, 0)       = t * axis.x * axis.y + s * axis.z;
            result(1, 1)       = t * axis.y * axis.y + c;
            result(1, 2)       = t * axis.y * axis.z - s * axis.x;
            result(2, 0)       = t * axis.x * axis.z - s * axis.y;
            result(2, 1)       = t * axis.y * axis.z + s * axis.x;
            result(2, 2)       = t * axis.z * axis.z + c;
            return result;
        }

        [[nodiscard]] Mat4f MakeOrientation(Vec3f x, Vec3f y, Vec3f z)
        {
            return MakeBasis({}, x, y, z);
        }

        [[nodiscard]] Mat4f InverseOrthonormalOrientation(Vec3f x, Vec3f y, Vec3f z)
        {
            Mat4f       result  = Identity<Mat4f>();
            const Vec3f axes[3] = {x, y, z};
            for (uint32_t row = 0; row < 3; ++row)
            {
                result(row, 0) = (&axes[row].x)[0];
                result(row, 1) = (&axes[row].x)[1];
                result(row, 2) = (&axes[row].x)[2];
            }
            return result;
        }

        [[nodiscard]] float DistanceToSegment(float x, float y, float ax, float ay, float bx, float by)
        {
            const float dx = bx - ax;
            const float dy = by - ay;
            const float l2 = dx * dx + dy * dy;
            if (l2 <= 1.0e-6f)
                return std::sqrt((x - ax) * (x - ax) + (y - ay) * (y - ay));
            const float t  = std::fmax(0.f, std::fmin(1.f, ((x - ax) * dx + (y - ay) * dy) / l2));
            const float px = ax + dx * t;
            const float py = ay + dy * t;
            return std::sqrt((x - px) * (x - px) + (y - py) * (y - py));
        }

        [[nodiscard]] bool TryProject(const FlyCamera& camera, const float rect[4], Vec3f point, float& out_x, float& out_y)
        {
            const Vec4f clip = camera.GetViewProjection() * Vec4f(point, 1.f);
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) || clip.w <= 1.0e-5f)
                return false;
            const float ndc_x = clip.x / clip.w;
            const float ndc_y = clip.y / clip.w;
            if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y))
                return false;
            out_x = rect[0] + (ndc_x * .5f + .5f) * (rect[2] - rect[0]);
            // The Vulkan projection already flips Y and the viewport has a
            // positive height, so NDC -1 is the top of the displayed image.
            out_y = rect[1] + (.5f + ndc_y * .5f) * (rect[3] - rect[1]);
            return std::isfinite(out_x) && std::isfinite(out_y);
        }

        [[nodiscard]] GizmoRay CursorRay(const ZEngine::UI::ZUIContext& ctx, const FlyCamera& camera, const float rect[4])
        {
            const float          input_x = ctx.InputScale[0] > 0.f ? ctx.InputScale[0] : 1.f;
            const float          input_y = ctx.InputScale[1] > 0.f ? ctx.InputScale[1] : 1.f;
            const FlyCamera::Ray ray     = camera.GetRayFromViewport((ctx.MousePos[0] - rect[0]) / input_x, (ctx.MousePos[1] - rect[1]) / input_y);
            return {.Origin = ray.Origin, .Direction = ray.Direction};
        }

        [[nodiscard]] bool IsInside(const float rect[4], float x, float y)
        {
            return x >= rect[0] && x <= rect[2] && y >= rect[1] && y <= rect[3];
        }

        [[nodiscard]] Vec3f AxisForHandle(GizmoHandle handle, Vec3f x, Vec3f y, Vec3f z)
        {
            switch (handle)
            {
                case GizmoHandle::AxisX:
                    return x;
                case GizmoHandle::AxisY:
                    return y;
                case GizmoHandle::AxisZ:
                    return z;
                default:
                    return {};
            }
        }

        [[nodiscard]] bool TrySignedScreenAngle(float start_x, float start_y, float current_x, float current_y, float& out_radians)
        {
            const float start_length2   = start_x * start_x + start_y * start_y;
            const float current_length2 = current_x * current_x + current_y * current_y;
            if (!std::isfinite(start_length2) || !std::isfinite(current_length2) || start_length2 <= 1.0e-4f || current_length2 <= 1.0e-4f)
                return false;

            const float cross_product = start_x * current_y - start_y * current_x;
            const float dot_product   = start_x * current_x + start_y * current_y;
            out_radians               = std::atan2(cross_product, dot_product);
            return std::isfinite(out_radians);
        }
    } // namespace

    void EditorGizmoController::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t maximum_targets)
    {
        m_roots.init(arena, maximum_targets);
        m_drag_targets.init(arena, maximum_targets);
        m_preview_world.init(arena, maximum_targets);
        m_operation = GizmoOperation::None;
        m_render    = {};
    }

    void EditorGizmoController::SetOperation(GizmoOperation operation)
    {
        if (m_drag.Active || operation == m_operation)
            return;
        m_operation = operation;
    }

    GizmoOperation EditorGizmoController::Operation() const
    {
        return m_operation;
    }

    void EditorGizmoController::SetCoordinateSpace(CoordinateSpace space)
    {
        if (!m_drag.Active)
            m_space = space;
    }

    EditorGizmoController::CoordinateSpace EditorGizmoController::Space() const
    {
        return m_space;
    }

    bool EditorGizmoController::IsManipulating() const
    {
        return m_drag.Active;
    }

    const GizmoRenderState& EditorGizmoController::RenderState() const
    {
        return m_render;
    }

    bool EditorGizmoController::CaptureDragCamera(const ZEngine::UI::ZUIContext& ctx, const FlyCamera& camera, const float viewport_rect[4])
    {
        DragCamera  snapshot     = {};
        const float input_x      = ctx.InputScale[0] > 0.f && std::isfinite(ctx.InputScale[0]) ? ctx.InputScale[0] : 1.f;
        const float input_y      = ctx.InputScale[1] > 0.f && std::isfinite(ctx.InputScale[1]) ? ctx.InputScale[1] : 1.f;
        const float width        = (viewport_rect[2] - viewport_rect[0]) / input_x;
        const float height       = (viewport_rect[3] - viewport_rect[1]) / input_y;
        const float projection_y = std::fabs(camera.GetProjection()(1, 1));

        if (!std::isfinite(width) || !std::isfinite(height) || width <= 1.f || height <= 1.f || !std::isfinite(projection_y) || projection_y <= 1.0e-5f || !std::isfinite(camera.AspectRatio) || camera.AspectRatio <= 1.0e-5f || !IsFinite(camera.GetPosition()))
            return false;

        if (!TryNormalize(camera.GetForward(), snapshot.Forward) || !TryNormalize(camera.GetRight(), snapshot.Right) || !TryNormalize(camera.GetUp(), snapshot.Up))
            return false;

        for (uint32_t index = 0; index < 4; ++index)
        {
            if (!std::isfinite(viewport_rect[index]))
                return false;
            snapshot.Rect[index] = viewport_rect[index];
        }
        snapshot.ViewProjection = camera.GetViewProjection();
        snapshot.Position       = camera.GetPosition();
        snapshot.InputScale[0]  = input_x;
        snapshot.InputScale[1]  = input_y;
        snapshot.ProjectionY    = projection_y;
        snapshot.AspectRatio    = camera.AspectRatio;
        snapshot.Orthographic   = camera.GetProjectionType() == CameraType::ORTHOGRAPHIC;
        snapshot.Valid          = true;
        m_drag.Camera           = snapshot;
        return true;
    }

    bool EditorGizmoController::CursorRayForDrag(const ZEngine::UI::ZUIContext& ctx, GizmoRay& out_ray) const
    {
        const DragCamera& camera = m_drag.Camera;
        if (!camera.Valid)
            return false;

        const float width  = (camera.Rect[2] - camera.Rect[0]) / camera.InputScale[0];
        const float height = (camera.Rect[3] - camera.Rect[1]) / camera.InputScale[1];
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 1.f || height <= 1.f)
            return false;

        const float viewport_x = (ctx.MousePos[0] - camera.Rect[0]) / camera.InputScale[0];
        const float viewport_y = (ctx.MousePos[1] - camera.Rect[1]) / camera.InputScale[1];
        const float ndc_x      = (viewport_x / width) * 2.f - 1.f;
        const float ndc_y      = 1.f - (viewport_y / height) * 2.f;
        if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y))
            return false;

        if (camera.Orthographic)
        {
            const float half_height = 1.f / camera.ProjectionY;
            const float half_width  = half_height * camera.AspectRatio;
            out_ray                 = {
                .Origin    = camera.Position + camera.Right * (ndc_x * half_width) + camera.Up * (ndc_y * half_height),
                .Direction = camera.Forward,
            };
            return IsFinite(out_ray.Origin);
        }

        const float tan_half  = 1.f / camera.ProjectionY;
        Vec3f       direction = camera.Right * (ndc_x * camera.AspectRatio * tan_half) + camera.Up * (ndc_y * tan_half) + camera.Forward;
        if (!TryNormalize(direction, direction))
            return false;
        out_ray = {.Origin = camera.Position, .Direction = direction};
        return IsFinite(out_ray.Origin);
    }

    bool EditorGizmoController::ProjectDragPoint(Vec3f point, float& out_x, float& out_y) const
    {
        const DragCamera& camera = m_drag.Camera;
        if (!camera.Valid)
            return false;

        const Vec4f clip = camera.ViewProjection * Vec4f(point, 1.f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) || clip.w <= 1.0e-5f)
            return false;
        const float ndc_x = clip.x / clip.w;
        const float ndc_y = clip.y / clip.w;
        if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y))
            return false;

        out_x = camera.Rect[0] + (ndc_x * .5f + .5f) * (camera.Rect[2] - camera.Rect[0]);
        out_y = camera.Rect[1] + (.5f + ndc_y * .5f) * (camera.Rect[3] - camera.Rect[1]);
        return std::isfinite(out_x) && std::isfinite(out_y);
    }

    bool EditorGizmoController::BuildRoots(const Scene& scene, ArrayView<const EntityID> selection)
    {
        m_roots.clear();
        for (size_t entity_index = 0; entity_index < selection.size(); ++entity_index)
        {
            const EntityID entity = selection[entity_index];
            if (!scene.IsAlive(entity) || !scene.HasComponent<TransformComponent>(entity))
                continue;
            bool     has_selected_ancestor = false;
            EntityID current               = entity;
            for (uint32_t depth = 0; depth < scene.AliveCount(); ++depth)
            {
                const ParentComponent* parent = scene.GetComponent<ParentComponent>(current);
                if (!parent || !parent->Parent.IsValid() || !scene.IsAlive(parent->Parent) || !scene.HasComponent<TransformComponent>(parent->Parent))
                    break;
                current = parent->Parent;
                for (size_t selection_index = 0; selection_index < selection.size(); ++selection_index)
                {
                    const EntityID selected = selection[selection_index];
                    if (selected == current)
                    {
                        has_selected_ancestor = true;
                        break;
                    }
                }
                if (has_selected_ancestor)
                    break;
            }
            if (!has_selected_ancestor && m_roots.size() < m_roots.capacity())
                m_roots.push(entity);
        }
        return !m_roots.empty();
    }

    bool EditorGizmoController::RebuildPresentation(EditorScene& editor_scene, Scene& scene, const FlyCamera& camera, const float viewport_rect[4], const float input_scale[2])
    {
        m_render = {};
        if (m_operation == GizmoOperation::None)
            return false;

        const ArrayView<const EntityID> selection = editor_scene.TransformSession.Selection();
        if (!BuildRoots(scene, selection))
            return false;

        Vec3f pivot = {};
        for (const EntityID entity : m_roots)
            pivot += MatrixTranslation(scene.GetComponent<TransformComponent>(entity)->WorldTransform);
        pivot                 /= static_cast<float>(m_roots.size());

        Vec3f          axis_x  = {1.f, 0.f, 0.f};
        Vec3f          axis_y  = {0.f, 1.f, 0.f};
        Vec3f          axis_z  = {0.f, 0.f, 1.f};
        const EntityID active  = editor_scene.TransformSession.ActiveEntity();
        if (m_space == CoordinateSpace::Local && active.IsValid())
        {
            const TransformComponent* transform   = scene.GetComponent<TransformComponent>(active);
            Vec3f                     candidate_x = {}, candidate_y = {}, candidate_z = {};
            if (transform && TryNormalize(MatrixColumn(transform->WorldTransform, 0), candidate_x))
            {
                candidate_y  = MatrixColumn(transform->WorldTransform, 1);
                candidate_y -= candidate_x * dot(candidate_y, candidate_x);
                if (TryNormalize(candidate_y, candidate_y) && TryNormalize(cross3d(candidate_x, candidate_y), candidate_z))
                {
                    // Preserve a reflected active transform's visible Z direction while
                    // still publishing an orthonormal basis for the drag math.
                    if (dot(candidate_z, MatrixColumn(transform->WorldTransform, 2)) < 0.f)
                        candidate_z = -candidate_z;
                    axis_x = candidate_x;
                    axis_y = candidate_y;
                    axis_z = candidate_z;
                }
            }
        }

        const float viewport_height = (viewport_rect[3] - viewport_rect[1]) / (input_scale[1] > 0.f ? input_scale[1] : 1.f);
        const float projection_y    = std::fabs(camera.GetProjection()(1, 1));
        if (!std::isfinite(viewport_height) || viewport_height <= 1.f || !std::isfinite(projection_y) || projection_y <= 1.0e-5f)
            return false;
        float scale = 2.f * GizmoPixels / (projection_y * viewport_height);
        if (camera.GetProjectionType() != ZEngine::Rendering::Cameras::CameraType::ORTHOGRAPHIC)
        {
            const float distance = (camera.GetPosition() - pivot).magnitude();
            if (!std::isfinite(distance) || distance <= 1.0e-4f)
                return false;
            scale *= distance;
        }
        if (!std::isfinite(scale) || scale <= 1.0e-5f)
            return false;

        m_render = {
            .Basis     = MakeBasis(pivot, axis_x, axis_y, axis_z),
            .Scale     = scale,
            .Operation = m_operation,
            .Hovered   = m_hovered,
            .Active    = m_drag.Active ? m_drag.Handle : GizmoHandle::None,
            .Visible   = true,
        };
        return true;
    }

    GizmoHandle EditorGizmoController::HitTest(const FlyCamera& camera, const float viewport_rect[4], float mouse_x, float mouse_y) const
    {
        if (!m_render.Visible)
            return GizmoHandle::None;
        const Vec3f pivot   = MatrixTranslation(m_render.Basis);
        const Vec3f axis_x  = MatrixColumn(m_render.Basis, 0);
        const Vec3f axis_y  = MatrixColumn(m_render.Basis, 1);
        const Vec3f axis_z  = MatrixColumn(m_render.Basis, 2);
        float       pivot_x = 0.f, pivot_y = 0.f;
        if (!TryProject(camera, viewport_rect, pivot, pivot_x, pivot_y))
            return GizmoHandle::None;

        if (m_render.Operation == GizmoOperation::Scale)
        {
            const float dx = mouse_x - pivot_x;
            const float dy = mouse_y - pivot_y;
            if (dx * dx + dy * dy <= UniformHitPixels * UniformHitPixels)
                return GizmoHandle::Uniform;
        }

        const Vec3f       axes[3]    = {axis_x, axis_y, axis_z};
        const GizmoHandle handles[3] = {GizmoHandle::AxisX, GizmoHandle::AxisY, GizmoHandle::AxisZ};

        if (m_render.Operation == GizmoOperation::Rotate)
        {
            float       nearest = RingHitPixels;
            GizmoHandle result  = GizmoHandle::None;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const Vec3f u          = axes[(axis + 1) % 3];
                const Vec3f v          = axes[(axis + 2) % 3];
                float       previous_x = 0.f, previous_y = 0.f;
                if (!TryProject(camera, viewport_rect, pivot + u * (m_render.Scale * .82f), previous_x, previous_y))
                    continue;
                for (uint32_t segment = 1; segment <= 48; ++segment)
                {
                    const float angle     = 6.28318530718f * static_cast<float>(segment) / 48.f;
                    float       current_x = 0.f, current_y = 0.f;
                    if (TryProject(camera, viewport_rect, pivot + (u * std::cos(angle) + v * std::sin(angle)) * (m_render.Scale * .82f), current_x, current_y))
                    {
                        const float distance = DistanceToSegment(mouse_x, mouse_y, previous_x, previous_y, current_x, current_y);
                        if (distance < nearest)
                        {
                            nearest = distance;
                            result  = handles[axis];
                        }
                        previous_x = current_x;
                        previous_y = current_y;
                    }
                }
            }
            return result;
        }

        if (m_render.Operation == GizmoOperation::Translate)
        {
            const Vec3f       plane_a[3]       = {axis_x, axis_x, axis_y};
            const Vec3f       plane_b[3]       = {axis_y, axis_z, axis_z};
            const GizmoHandle plane_handles[3] = {GizmoHandle::PlaneXY, GizmoHandle::PlaneXZ, GizmoHandle::PlaneYZ};
            for (uint32_t plane = 0; plane < 3; ++plane)
            {
                float center_x = 0.f, center_y = 0.f;
                if (TryProject(camera, viewport_rect, pivot + (plane_a[plane] + plane_b[plane]) * (m_render.Scale * .39f), center_x, center_y))
                {
                    const float dx = mouse_x - center_x;
                    const float dy = mouse_y - center_y;
                    if (dx * dx + dy * dy <= PlaneHitPixels * PlaneHitPixels)
                        return plane_handles[plane];
                }
            }
        }

        float       nearest = AxisHitPixels;
        GizmoHandle result  = GizmoHandle::None;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            float start_x = 0.f, start_y = 0.f, end_x = 0.f, end_y = 0.f;
            if (!TryProject(camera, viewport_rect, pivot + axes[axis] * (m_render.Scale * .12f), start_x, start_y) || !TryProject(camera, viewport_rect, pivot + axes[axis] * (m_render.Scale * 1.12f), end_x, end_y))
                continue;
            const float distance = DistanceToSegment(mouse_x, mouse_y, start_x, start_y, end_x, end_y);
            if (distance < nearest)
            {
                nearest = distance;
                result  = handles[axis];
            }
        }
        return result;
    }

    bool EditorGizmoController::BeginDrag(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, Scene& scene, const FlyCamera& camera, const float viewport_rect[4], uint64_t viewport_key, uint64_t viewport_generation, GizmoHandle handle)
    {
        if (!ctx || handle == GizmoHandle::None || m_drag.Active || !editor_scene.TransformSession.Begin(scene))
            return false;

        const ZEngine::UI::ZUIPointerCaptureToken capture = ZUICapturePointer(ctx, GizmoOwnerKey, viewport_key, viewport_generation, 0);
        if (capture == 0)
        {
            (void) editor_scene.TransformSession.Cancel(scene);
            return false;
        }

        m_drag               = {};
        m_drag.CaptureToken  = capture;
        m_drag.Handle        = handle;
        m_drag.Pivot         = MatrixTranslation(m_render.Basis);
        m_drag.AxisA         = AxisForHandle(handle, MatrixColumn(m_render.Basis, 0), MatrixColumn(m_render.Basis, 1), MatrixColumn(m_render.Basis, 2));
        m_drag.GizmoScale    = m_render.Scale;
        m_drag.Active        = true;
        m_drag.StartMouse[0] = ctx->MousePos[0];
        m_drag.StartMouse[1] = ctx->MousePos[1];

        if (handle == GizmoHandle::PlaneXY)
        {
            m_drag.AxisA      = MatrixColumn(m_render.Basis, 0);
            m_drag.AxisB      = MatrixColumn(m_render.Basis, 1);
            m_drag.AxisNormal = MatrixColumn(m_render.Basis, 2);
        }
        else if (handle == GizmoHandle::PlaneXZ)
        {
            m_drag.AxisA      = MatrixColumn(m_render.Basis, 0);
            m_drag.AxisB      = MatrixColumn(m_render.Basis, 2);
            m_drag.AxisNormal = MatrixColumn(m_render.Basis, 1);
        }
        else if (handle == GizmoHandle::PlaneYZ)
        {
            m_drag.AxisA      = MatrixColumn(m_render.Basis, 1);
            m_drag.AxisB      = MatrixColumn(m_render.Basis, 2);
            m_drag.AxisNormal = MatrixColumn(m_render.Basis, 0);
        }
        else if (handle == GizmoHandle::AxisX || handle == GizmoHandle::AxisY || handle == GizmoHandle::AxisZ)
        {
            const Vec3f basis_x = MatrixColumn(m_render.Basis, 0);
            const Vec3f basis_y = MatrixColumn(m_render.Basis, 1);
            const Vec3f basis_z = MatrixColumn(m_render.Basis, 2);
            m_drag.AxisB        = std::fabs(dot(m_drag.AxisA, basis_x)) < .5f ? basis_x : basis_y;
            m_drag.AxisNormal   = cross3d(m_drag.AxisA, m_drag.AxisB);
            if (!TryNormalize(m_drag.AxisNormal, m_drag.AxisNormal))
                m_drag.AxisNormal = basis_z;
        }

        if (!CaptureDragCamera(*ctx, camera, viewport_rect))
        {
            FinishDrag(ctx, editor_scene, scene, false);
            return false;
        }

        m_drag_targets.clear();
        const auto active_targets = editor_scene.TransformSession.ActiveTargets();
        for (size_t target_index = 0; target_index < active_targets.size(); ++target_index)
        {
            const auto&               record    = active_targets[target_index];
            const TransformComponent* transform = scene.GetComponent<TransformComponent>(record.Entity);
            if (!transform || m_drag_targets.size() == m_drag_targets.capacity())
            {
                FinishDrag(ctx, editor_scene, scene, false);
                return false;
            }
            m_drag_targets.push({.Entity = record.Entity, .InitialWorld = transform->WorldTransform});
        }

        GizmoRay ray = {};
        if (!CursorRayForDrag(*ctx, ray))
        {
            FinishDrag(ctx, editor_scene, scene, false);
            return false;
        }
        if (m_operation == GizmoOperation::Translate && (handle == GizmoHandle::AxisX || handle == GizmoHandle::AxisY || handle == GizmoHandle::AxisZ))
        {
            if (!TryClosestRayAxisParameter(ray, m_drag.Pivot, m_drag.AxisA, m_drag.StartScalar))
            {
                Vec3f point = {};
                if (!TryIntersectRayPlane(ray, {.Point = m_drag.Pivot, .Normal = m_drag.Camera.Forward}, point))
                {
                    FinishDrag(ctx, editor_scene, scene, false);
                    return false;
                }
                m_drag.StartScalar = dot(point - m_drag.Pivot, m_drag.AxisA);
            }
        }
        else if (m_operation == GizmoOperation::Translate && handle != GizmoHandle::Uniform)
        {
            if (!TryIntersectRayPlane(ray, {.Point = m_drag.Pivot, .Normal = m_drag.AxisNormal}, m_drag.StartPoint))
            {
                FinishDrag(ctx, editor_scene, scene, false);
                return false;
            }
        }
        else if (m_operation == GizmoOperation::Rotate)
        {
            float pivot_x = 0.f, pivot_y = 0.f;
            if (!ProjectDragPoint(m_drag.Pivot, pivot_x, pivot_y))
            {
                FinishDrag(ctx, editor_scene, scene, false);
                return false;
            }
            m_drag.StartScreen[0] = ctx->MousePos[0] - pivot_x;
            m_drag.StartScreen[1] = ctx->MousePos[1] - pivot_y;
            if (m_drag.StartScreen[0] * m_drag.StartScreen[0] + m_drag.StartScreen[1] * m_drag.StartScreen[1] <= 1.0e-4f)
            {
                FinishDrag(ctx, editor_scene, scene, false);
                return false;
            }

            // The world-space path has the best feel whenever the ring plane is
            // visible. A projected-angle fallback keeps edge-on rings usable.
            Vec3f point             = {};
            m_drag.UseWorldRotation = TryIntersectRayPlane(ray, {.Point = m_drag.Pivot, .Normal = m_drag.AxisA}, point);
            if (m_drag.UseWorldRotation)
                m_drag.StartPoint = point - m_drag.Pivot;

            float axis_b_x = 0.f, axis_b_y = 0.f, axis_n_x = 0.f, axis_n_y = 0.f;
            if (ProjectDragPoint(m_drag.Pivot + m_drag.AxisB * (m_drag.GizmoScale * .82f), axis_b_x, axis_b_y) && ProjectDragPoint(m_drag.Pivot + m_drag.AxisNormal * (m_drag.GizmoScale * .82f), axis_n_x, axis_n_y))
            {
                const float winding = (axis_b_x - pivot_x) * (axis_n_y - pivot_y) - (axis_b_y - pivot_y) * (axis_n_x - pivot_x);
                if (std::isfinite(winding) && std::fabs(winding) > 1.0e-4f)
                    m_drag.ScreenRotationSign = winding < 0.f ? -1.f : 1.f;
            }
        }
        else if (m_operation == GizmoOperation::Scale && handle == GizmoHandle::Uniform)
        {
            // A center handle has no stable initial radius. Use signed screen
            // displacement instead, so clicking its center cannot cause a jump.
            m_drag.StartScreen[0] = ctx->MousePos[0];
            m_drag.StartScreen[1] = ctx->MousePos[1];
        }
        else if (m_operation == GizmoOperation::Scale)
        {
            if (!TryClosestRayAxisParameter(ray, m_drag.Pivot, m_drag.AxisA, m_drag.StartScalar))
                m_drag.StartScalar = 0.f;
        }
        return true;
    }

    bool EditorGizmoController::PreviewDrag(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, Scene& scene)
    {
        if (!ctx || !m_drag.Active)
            return false;
        if (!m_drag.Previewed && ctx->MousePos[0] == m_drag.StartMouse[0] && ctx->MousePos[1] == m_drag.StartMouse[1])
            return true;
        GizmoRay   ray     = {};
        const bool has_ray = CursorRayForDrag(*ctx, ray);
        Mat4f      delta   = Identity<Mat4f>();

        if (m_operation == GizmoOperation::Translate)
        {
            if (!has_ray)
                return false;
            Vec3f translation = {};
            if (m_drag.Handle == GizmoHandle::AxisX || m_drag.Handle == GizmoHandle::AxisY || m_drag.Handle == GizmoHandle::AxisZ)
            {
                float scalar = 0.f;
                if (!TryClosestRayAxisParameter(ray, m_drag.Pivot, m_drag.AxisA, scalar))
                {
                    Vec3f point = {};
                    if (!TryIntersectRayPlane(ray, {.Point = m_drag.Pivot, .Normal = m_drag.Camera.Forward}, point))
                        return true;
                    scalar = dot(point - m_drag.Pivot, m_drag.AxisA);
                }
                translation = m_drag.AxisA * SnapGizmoScalar(scalar - m_drag.StartScalar, TranslationSnap, ctx->CtrlDown);
            }
            else
            {
                Vec3f point = {};
                if (!TryIntersectRayPlane(ray, {.Point = m_drag.Pivot, .Normal = m_drag.AxisNormal}, point))
                    return true;
                const Vec3f raw = point - m_drag.StartPoint;
                translation     = m_drag.AxisA * SnapGizmoScalar(dot(raw, m_drag.AxisA), TranslationSnap, ctx->CtrlDown) + m_drag.AxisB * SnapGizmoScalar(dot(raw, m_drag.AxisB), TranslationSnap, ctx->CtrlDown);
            }
            delta = MakeTranslation(translation);
        }
        else if (m_operation == GizmoOperation::Rotate)
        {
            float angle    = 0.f;
            bool  resolved = false;
            if (m_drag.UseWorldRotation && has_ray)
            {
                Vec3f point = {};
                if (TryIntersectRayPlane(ray, {.Point = m_drag.Pivot, .Normal = m_drag.AxisA}, point) && TrySignedAxisAngle(m_drag.AxisA, m_drag.StartPoint, point - m_drag.Pivot, angle))
                    resolved = true;
            }
            if (!resolved)
            {
                float pivot_x = 0.f, pivot_y = 0.f;
                if (!ProjectDragPoint(m_drag.Pivot, pivot_x, pivot_y) || !TrySignedScreenAngle(m_drag.StartScreen[0], m_drag.StartScreen[1], ctx->MousePos[0] - pivot_x, ctx->MousePos[1] - pivot_y, angle))
                    return true;
                angle *= m_drag.ScreenRotationSign;
            }
            angle = SnapGizmoScalar(angle, RotationSnapRadians, ctx->CtrlDown);
            delta = MakeTranslation(m_drag.Pivot) * MakeAxisRotation(m_drag.AxisA, angle) * MakeTranslation(-m_drag.Pivot);
        }
        else if (m_operation == GizmoOperation::Scale)
        {
            float ratio = 1.f;
            if (m_drag.Handle == GizmoHandle::Uniform)
            {
                ratio = 1.f + ((ctx->MousePos[0] - m_drag.StartScreen[0]) - (ctx->MousePos[1] - m_drag.StartScreen[1])) / GizmoPixels;
            }
            else
            {
                if (!has_ray)
                    return false;
                float scalar = 0.f;
                if (!TryClosestRayAxisParameter(ray, m_drag.Pivot, m_drag.AxisA, scalar))
                    return true;
                ratio = 1.f + (scalar - m_drag.StartScalar) / (m_drag.GizmoScale * .72f);
                if (!std::isfinite(ratio) || ratio <= 1.0e-3f)
                    ratio = 1.0e-3f;
            }
            ratio = 1.f + SnapGizmoScalar(ratio - 1.f, ScaleSnap, ctx->CtrlDown);
            ratio = std::fmax(1.0e-3f, ratio);
            if (m_drag.Handle == GizmoHandle::Uniform)
                delta = MakeTranslation(m_drag.Pivot) * MakeScale({ratio, ratio, ratio}) * MakeTranslation(-m_drag.Pivot);
            else
            {
                const Mat4f orientation = MakeOrientation(m_drag.AxisA, m_drag.AxisB, m_drag.AxisNormal);
                const Mat4f inverse     = InverseOrthonormalOrientation(m_drag.AxisA, m_drag.AxisB, m_drag.AxisNormal);
                delta                   = MakeTranslation(m_drag.Pivot) * orientation * MakeScale({ratio, 1.f, 1.f}) * inverse * MakeTranslation(-m_drag.Pivot);
            }
        }

        m_preview_world.clear();
        for (const DragTarget& target : m_drag_targets)
            m_preview_world.push(delta * target.InitialWorld);
        if (m_preview_world.size() != m_drag_targets.size() || !editor_scene.TransformSession.PreviewWorld(scene, {m_preview_world.data(), m_preview_world.size()}))
            return false;
        m_drag.Previewed = true;
        if (m_operation == GizmoOperation::Translate)
        {
            // Keep the visible gizmo attached to the edited selection while the
            // constraint math continues to use its frozen initial pivot/camera.
            const Vec4f pivot    = delta * Vec4f(m_drag.Pivot, 1.f);
            m_render.Basis(0, 3) = pivot.x;
            m_render.Basis(1, 3) = pivot.y;
            m_render.Basis(2, 3) = pivot.z;
        }
        return true;
    }

    void EditorGizmoController::FinishDrag(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, Scene& scene, bool commit)
    {
        if (!m_drag.Active)
            return;
        const ZEngine::UI::ZUIPointerCaptureToken token = m_drag.CaptureToken;
        if (commit)
        {
            if (editor_scene.TransformSession.Commit(scene) && editor_scene.TransformSession.LastCommitChanged())
                editor_scene.MarkDirty(true);
        }
        else
            (void) editor_scene.TransformSession.Cancel(scene);
        if (ctx && ZUIHasPointerCapture(ctx, token))
        {
            if (commit)
                (void) ZUIReleasePointerCapture(ctx, token);
            else
                (void) ZUICancelPointerCapture(ctx, token, ZEngine::UI::ZUIPointerCaptureEndReason::ExplicitCancellation);
        }
        // A terminal lease event intentionally prevents immediate recapture.
        // Preserve its token after clearing the drag so the next update consumes
        // the event and opens the viewport for the next manipulation.
        if (ctx && ctx->PointerCaptureEnd.Pending && ctx->PointerCaptureEnd.Token == token)
            m_pending_capture_end = token;
        ResetDrag();
    }

    void EditorGizmoController::ConsumePointerEnd(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, Scene& scene)
    {
        if (!ctx)
            return;

        const ZEngine::UI::ZUIPointerCaptureToken token = m_drag.Active ? m_drag.CaptureToken : m_pending_capture_end;
        if (token == 0)
            return;

        ZEngine::UI::ZUIPointerCaptureEndReason reason = ZEngine::UI::ZUIPointerCaptureEndReason::None;
        if (!ZUIConsumePointerCaptureEnd(ctx, token, reason))
        {
            // A different owner may already have consumed an event during an
            // orderly shutdown. Do not leave this controller permanently stuck
            // behind a stale token.
            if (m_pending_capture_end == token && (!ctx->PointerCaptureEnd.Pending || ctx->PointerCaptureEnd.Token != token))
                m_pending_capture_end = 0;
            return;
        }

        m_pending_capture_end = 0;
        if (!m_drag.Active)
            return;

        if (reason == ZEngine::UI::ZUIPointerCaptureEndReason::Released)
        {
            // Release can arrive while UI payload production is backpressured.
            // Apply the latest cursor position before committing the transaction.
            if (!PreviewDrag(ctx, editor_scene, scene))
                (void) editor_scene.TransformSession.Cancel(scene);
            else if (editor_scene.TransformSession.Commit(scene) && editor_scene.TransformSession.LastCommitChanged())
                editor_scene.MarkDirty(true);
        }
        else
            (void) editor_scene.TransformSession.Cancel(scene);
        ResetDrag();
    }

    void EditorGizmoController::ResetDrag()
    {
        m_drag = {};
        m_drag_targets.clear();
        m_preview_world.clear();
    }

    void EditorGizmoController::UpdateInputCapture(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, Scene& scene)
    {
        if (!ctx)
            return;
        ConsumePointerEnd(ctx, editor_scene, scene);
        if (m_drag.Active && (ctx->EscapePressed || ctx->ActiveModalKey != 0 || ctx->ModalBox != nullptr || ctx->PopupStackSize != 0))
            FinishDrag(ctx, editor_scene, scene, false);
    }

    void EditorGizmoController::Cancel(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, Scene& scene)
    {
        FinishDrag(ctx, editor_scene, scene, false);
        ConsumePointerEnd(ctx, editor_scene, scene);
        m_hovered = GizmoHandle::None;
        m_render  = {};
    }

    void EditorGizmoController::Update(ZEngine::UI::ZUIContext* ctx, EditorScene& editor_scene, Scene& scene, const FlyCamera& camera, const float viewport_rect[4], uint64_t viewport_key, uint64_t viewport_generation)
    {
        if (!ctx || viewport_key == 0 || viewport_generation == 0)
            return;

        UpdateInputCapture(ctx, editor_scene, scene);

        if (!m_drag.Active)
        {
            (void) RebuildPresentation(editor_scene, scene, camera, viewport_rect, ctx->InputScale);
            const bool input_available = ctx->ActiveModalKey == 0 && ctx->ModalBox == nullptr && ctx->PopupStackSize == 0 && !ctx->PointerCapture.Active && !ctx->PointerCaptureEnd.Pending && !ctx->EscapePressed && (ctx->HotKey == 0 || ctx->HotKey == viewport_key) && (ctx->ActiveKey == 0 || ctx->ActiveKey == viewport_key) && ctx->DragSourceKey == 0 && IsInside(viewport_rect, ctx->MousePos[0], ctx->MousePos[1]);
            m_hovered                  = input_available ? HitTest(camera, viewport_rect, ctx->MousePos[0], ctx->MousePos[1]) : GizmoHandle::None;
            m_render.Hovered           = m_hovered;
            if (ctx->MousePressed[0] && input_available && m_hovered != GizmoHandle::None)
                (void) BeginDrag(ctx, editor_scene, scene, camera, viewport_rect, viewport_key, viewport_generation, m_hovered);
            else if (ctx->MousePressed[0] && input_available)
            {
                const GizmoRay ray = CursorRay(*ctx, camera, viewport_rect);
                const auto     hit = ZEngine::Rendering::Scenes::RaycastSceneGeometry(editor_scene, ray.Origin, ray.Direction, camera.Settings.FarPlane);
                if (hit.Hit)
                    (void) editor_scene.SelectRenderInstance(scene, hit.InstanceId, ctx->CtrlDown);
                else if (!ctx->CtrlDown)
                    editor_scene.ClearSelection();
                (void) RebuildPresentation(editor_scene, scene, camera, viewport_rect, ctx->InputScale);
            }
            return;
        }

        m_render.Active  = m_drag.Handle;
        m_render.Hovered = m_drag.Handle;
        if (ctx->MouseReleased[0])
        {
            FinishDrag(ctx, editor_scene, scene, PreviewDrag(ctx, editor_scene, scene));
            return;
        }
        if (!PreviewDrag(ctx, editor_scene, scene))
            FinishDrag(ctx, editor_scene, scene, false);
    }
} // namespace Tetragrama::Gizmos
