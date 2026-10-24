#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoCamera.h>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;

namespace Tetragrama::Gizmo
{
    namespace
    {
        bool Unit(double x, double y, double z, Vec3f& output)
        {
            const double length = std::hypot(x, y, z);
            if (!std::isfinite(length) || length <= 0.0)
            {
                return false;
            }
            output = {float(x / length), float(y / length), float(z / length)};
            return true;
        }
    } // namespace
    bool ResolveCameraFrame(EditorSession& session, CameraFrame& output)
    {
        output = {};
        if (!session.OnOwnerThread() || !session.Editable() || session.Selection().ActiveSceneTarget() != SceneTarget::None || !session.Selection().Contains(session.Selection().ActiveUUID()))
        {
            return false;
        }
        const auto  entity = session.Resolve(session.Selection().ActiveUUID());
        const auto* scene  = session.Scene();
        if (!scene->IsAlive(entity))
        {
            return false;
        }
        const auto* transform = scene->GetComponent<Components::TransformComponent>(entity);
        const auto* camera    = scene->GetComponent<Components::CameraComponent>(entity);
        const auto* node      = scene->Hierarchy().Find(entity);
        if (!transform || !camera || !node || !node->FrameValid)
        {
            return false;
        }
        CameraFrame result = {.Target = session.TargetFor(entity), .World = transform->WorldTransform, .Pose = CaptureLocalTransform(*transform), .Camera = CaptureAuthoredCamera(*camera)};
        if (!ValidLocalTransform(result.Pose) || !ValidAuthoredCamera(result.Camera))
        {
            return false;
        }
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                if (!std::isfinite(result.World(row, column)))
                {
                    return false;
                }
            }
        }
        result.Origin = {result.World(0, 3), result.World(1, 3), result.World(2, 3)};
        // FlyCamera's identity orientation looks down -Z. Authored distances
        // remain world-sized; strip scale/shear without inventing zero forward.
        if (!Unit(-double(result.World(0, 2)), -double(result.World(1, 2)), -double(result.World(2, 2)), result.Forward))
        {
            return false;
        }
        const Vec3f  right(result.World(0, 0), result.World(1, 0), result.World(2, 0));
        const double along = double(right.x) * result.Forward.x + double(right.y) * result.Forward.y + double(right.z) * result.Forward.z;
        if (!Unit(double(right.x) - along * result.Forward.x, double(right.y) - along * result.Forward.y, double(right.z) - along * result.Forward.z, result.Right))
        {
            const Vec3f reference = std::abs(result.Forward.y) < Constants::AxisReferenceParallelY ? Vec3f(0.f, 1.f, 0.f) : Vec3f(1.f, 0.f, 0.f);
            const auto  fallback  = cross3d(result.Forward, reference);
            if (!Unit(fallback.x, fallback.y, fallback.z, result.Right))
            {
                return false;
            }
        }
        const auto up = cross3d(result.Right, result.Forward);
        if (!Unit(up.x, up.y, up.z, result.Up))
        {
            return false;
        }
        result.SelectionRevision   = session.Selection().Revision();
        result.StructuralRevision  = scene->StructuralRevision();
        result.HierarchyRevision   = scene->Hierarchy().Revision();
        result.WorldRevision       = node->WorldRevision;
        result.ParentWorldRevision = scene->Hierarchy().ParentWorldRevision(entity);
        result.Valid               = true;
        output                     = result;
        return true;
    }
    bool SameCameraFrame(const CameraFrame& a, const CameraFrame& b)
    {
        if (a.Valid != b.Valid)
        {
            return false;
        }
        if (!a.Valid)
        {
            return true;
        }
        if (a.Target.UUID != b.Target.UUID || a.Target.CachedEntity != b.Target.CachedEntity || a.Target.SceneEpoch != b.Target.SceneEpoch || a.SelectionRevision != b.SelectionRevision || a.StructuralRevision != b.StructuralRevision || a.HierarchyRevision != b.HierarchyRevision || a.WorldRevision != b.WorldRevision || a.ParentWorldRevision != b.ParentWorldRevision || !SameLocalTransform(a.Pose, b.Pose) || a.Camera.FovY != b.Camera.FovY || a.Camera.Near != b.Camera.Near || a.Camera.Far != b.Camera.Far || a.Camera.AspectRatio != b.Camera.AspectRatio ||
            a.Camera.IsMain != b.Camera.IsMain)
        {
            return false;
        }
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                if (a.World(row, column) != b.World(row, column))
                {
                    return false;
                }
            }
        }
        return true;
    }
    bool ValidCameraGeometry(const CameraFrame& frame)
    {
        if (!frame.Valid || !ValidAuthoredCamera(frame.Camera))
        {
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(frame.Origin[axis]))
            {
                return false;
            }
        }
        const Vec3f axes[] = {frame.Right, frame.Up, frame.Forward};
        for (uint32_t i = 0; i < 3; ++i)
        {
            double length = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                length += double(axes[i][axis]) * axes[i][axis];
            }
            if (!std::isfinite(length) || std::abs(length - 1.0) > Constants::UnitAxisTolerance)
            {
                return false;
            }
            for (uint32_t j = 0; j < i; ++j)
            {
                double along = 0.0;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    along += double(axes[i][axis]) * axes[j][axis];
                }
                if (!std::isfinite(along) || std::abs(along) > Constants::UnitAxisTolerance)
                {
                    return false;
                }
            }
        }
        return true;
    }
    bool CameraPoint(const CameraFrame& frame, double right, double up, double forward, Vec3f& output)
    {
        Vec3f result = {};
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const double value = double(frame.Origin[axis]) + double(frame.Right[axis]) * right + double(frame.Up[axis]) * up + double(frame.Forward[axis]) * forward;
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            {
                return false;
            }
            result[axis] = float(value);
        }
        output = result;
        return true;
    }
    bool CameraCorner(const CameraFrame& frame, uint32_t corner, double distance, Vec3f& output)
    {
        if (corner >= 4 || !std::isfinite(distance) || distance <= 0.0)
        {
            return false;
        }
        const double half_height = distance * std::tan(double(frame.Camera.FovY) * Constants::HalfRadiansPerDegree);
        return CameraPoint(frame, (corner & 1 ? 1.0 : -1.0) * half_height * frame.Camera.AspectRatio, (corner & 2 ? -1.0 : 1.0) * half_height, distance, output);
    }
    bool ResolveCameraHandle(const CameraFrame& frame, Handle handle, CameraHandle& output)
    {
        if (!IsCameraHandle(handle) || !ValidCameraGeometry(frame))
        {
            return false;
        }
        const uint32_t index         = uint32_t(handle) - uint32_t(Handle::CameraFovUpperLeft);
        const uint32_t corner        = index;
        CameraHandle   result        = {};
        const bool     near          = handle == Handle::CameraNearLeft || handle == Handle::CameraNearRight;
        const double   distance      = near ? frame.Camera.Near : frame.Camera.Far;
        const double   tangent       = std::tan(double(frame.Camera.FovY) * Constants::HalfRadiansPerDegree);
        const double   aspect_length = std::hypot(1.0, double(frame.Camera.AspectRatio));
        if (near)
        {
            const double sign = handle == Handle::CameraNearRight ? 1.0 : -1.0;
            if (!CameraPoint(frame, sign * distance * tangent * frame.Camera.AspectRatio, 0.0, distance, result.Position))
            {
                return false;
            }
            result.Field     = AuthoredFloatField::CameraNear;
            result.Factor    = std::hypot(1.0, tangent * frame.Camera.AspectRatio);
            result.Parameter = distance;
            if (!Unit(double(frame.Forward.x) + sign * tangent * frame.Camera.AspectRatio * frame.Right.x, double(frame.Forward.y) + sign * tangent * frame.Camera.AspectRatio * frame.Right.y, double(frame.Forward.z) + sign * tangent * frame.Camera.AspectRatio * frame.Right.z, result.Direction))
            {
                return false;
            }
        }
        else if (!CameraCorner(frame, corner, distance, result.Position))
        {
            return false;
        }
        else if (index < 2)
        {
            result.Field     = AuthoredFloatField::CameraFovY;
            result.Factor    = double(frame.Camera.Far) * aspect_length;
            result.Parameter = result.Factor * tangent;
            if (!Unit(double(frame.Right.x) * (corner & 1 ? 1.0 : -1.0) * frame.Camera.AspectRatio + frame.Up.x, double(frame.Right.y) * (corner & 1 ? 1.0 : -1.0) * frame.Camera.AspectRatio + frame.Up.y, double(frame.Right.z) * (corner & 1 ? 1.0 : -1.0) * frame.Camera.AspectRatio + frame.Up.z, result.Direction))
            {
                return false;
            }
        }
        else
        {
            result.Field     = AuthoredFloatField::CameraFar;
            result.Factor    = std::hypot(1.0, tangent * aspect_length);
            result.Parameter = distance;
            if (!Unit(double(frame.Forward.x) + tangent * ((corner & 1 ? 1.0 : -1.0) * frame.Camera.AspectRatio * frame.Right.x - frame.Up.x), double(frame.Forward.y) + tangent * ((corner & 1 ? 1.0 : -1.0) * frame.Camera.AspectRatio * frame.Right.y - frame.Up.y), double(frame.Forward.z) + tangent * ((corner & 1 ? 1.0 : -1.0) * frame.Camera.AspectRatio * frame.Right.z - frame.Up.z), result.Direction))
            {
                return false;
            }
        }
        if (!std::isfinite(result.Parameter) || !std::isfinite(result.Factor) || result.Factor <= 0.0)
        {
            return false;
        }
        output = result;
        return true;
    }
} // namespace Tetragrama::Gizmo
