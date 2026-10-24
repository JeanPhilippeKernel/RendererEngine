#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoLight.h>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using Type = Components::LightComponent::Type;

namespace Tetragrama::Gizmo
{
    namespace
    {
        bool Unit(const Vec3f& value, Vec3f& output)
        {
            const double length = std::hypot(double(value.x), double(value.y), double(value.z));
            if (!std::isfinite(length) || length <= 0.0)
            {
                return false;
            }
            output = {float(value.x / length), float(value.y / length), float(value.z / length)};
            return true;
        }

        bool Point(const Vec3f& origin, const Vec3f& direction, double distance, Vec3f& output)
        {
            Vec3f point = {};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const double value = double(origin[axis]) + double(direction[axis]) * distance;
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                {
                    return false;
                }
                point[axis] = float(value);
            }
            output = point;
            return true;
        }
    } // namespace

    bool ResolveLightFrame(EditorSession& session, LightFrame& output)
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
        const auto* light     = scene->GetComponent<Components::LightComponent>(entity);
        const auto* node      = scene->Hierarchy().Find(entity);
        if (!scene->IsAlive(entity) || !transform || !light || !node || !node->FrameValid)
        {
            return false;
        }
        LightFrame result = {.Target = session.TargetFor(entity), .World = transform->WorldTransform, .Pose = CaptureLocalTransform(*transform), .Light = CaptureAuthoredLight(*light)};
        if (!ValidAuthoredLight(result.Light) || !ValidLocalTransform(result.Pose))
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
        if (result.Light.Type != Type::Point)
        {
            // LightSyncSystem uses +Z, including parent orientation/reflection.
            // Orthonormalize the section plane: Range is world-sized, not a
            // scaled/sheared ellipsoid. A zero forward direction is not invented.
            if (!Unit({result.World(0, 2), result.World(1, 2), result.World(2, 2)}, result.Forward))
            {
                return false;
            }
            const Vec3f  right(result.World(0, 0), result.World(1, 0), result.World(2, 0));
            const double along = double(right.x) * result.Forward.x + double(right.y) * result.Forward.y + double(right.z) * result.Forward.z;
            if (!Unit({float(double(right.x) - along * result.Forward.x), float(double(right.y) - along * result.Forward.y), float(double(right.z) - along * result.Forward.z)}, result.Right))
            {
                const Vec3f reference = std::abs(result.Forward.y) < Constants::AxisReferenceParallelY ? Vec3f(0.f, 1.f, 0.f) : Vec3f(1.f, 0.f, 0.f);
                if (!Unit(cross3d(reference, result.Forward), result.Right))
                {
                    return false;
                }
            }
            if (!Unit(cross3d(result.Forward, result.Right), result.Up))
            {
                return false;
            }
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

    bool SameLightFrame(const LightFrame& a, const LightFrame& b)
    {
        if (a.Valid != b.Valid)
        {
            return false;
        }
        if (!a.Valid)
        {
            return true;
        }
        if (a.Target.UUID != b.Target.UUID || a.Target.CachedEntity != b.Target.CachedEntity || a.Target.SceneEpoch != b.Target.SceneEpoch || a.SelectionRevision != b.SelectionRevision || a.StructuralRevision != b.StructuralRevision || a.HierarchyRevision != b.HierarchyRevision || a.WorldRevision != b.WorldRevision || a.ParentWorldRevision != b.ParentWorldRevision || !SameLocalTransform(a.Pose, b.Pose) || a.Light.Type != b.Light.Type || a.Light.Range != b.Light.Range || a.Light.SpotAngle != b.Light.SpotAngle || a.Light.Intensity != b.Light.Intensity)
        {
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (a.Light.Color[axis] != b.Light.Color[axis])
            {
                return false;
            }
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

    bool ValidLightGeometry(const LightFrame& frame)
    {
        if (!frame.Valid || !ValidAuthoredLight(frame.Light))
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
        if (frame.Light.Type == Type::Point)
        {
            return true; // point ranges are independent of the transform basis
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

    bool ResolveLightHandle(const LightFrame& frame, Handle handle, LightHandle& output)
    {
        if (!ValidLightGeometry(frame))
        {
            return false;
        }
        LightHandle result = {.Parameter = frame.Light.Range};
        if (frame.Light.Type == Type::Point && handle >= Handle::LightRangePositiveX && handle <= Handle::LightRangeNegativeZ)
        {
            switch (handle)
            {
                case Handle::LightRangePositiveX:
                    result.Direction.x = 1.f;
                    break;
                case Handle::LightRangeNegativeX:
                    result.Direction.x = -1.f;
                    break;
                case Handle::LightRangePositiveY:
                    result.Direction.y = 1.f;
                    break;
                case Handle::LightRangePositiveZ:
                    result.Direction.z = 1.f;
                    break;
                case Handle::LightRangeNegativeZ:
                    result.Direction.z = -1.f;
                    break;
                default:
                    return false;
            }
            if (!Point(frame.Origin, result.Direction, result.Parameter, result.Position))
            {
                return false;
            }
        }
        else if (frame.Light.Type == Type::Spot && (handle == Handle::SpotRange || handle == Handle::SpotOuterAngle))
        {
            if (!Point(frame.Origin, frame.Forward, frame.Light.Range, result.Position))
            {
                return false;
            }
            result.Direction = frame.Forward;
            if (handle == Handle::SpotOuterAngle)
            {
                const double radius = double(frame.Light.Range) * std::tan(double(frame.Light.SpotAngle) * Constants::HalfRadiansPerDegree);
                if (!std::isfinite(radius) || radius > std::numeric_limits<float>::max() || !Point(result.Position, frame.Up, radius, result.Position))
                {
                    return false;
                }
                result.Parameter = float(radius);
                result.Direction = frame.Up;
                result.Field     = AuthoredFloatField::LightSpotAngle;
            }
        }
        else
        {
            return false;
        }
        output = result;
        return true;
    }
} // namespace Tetragrama::Gizmo
