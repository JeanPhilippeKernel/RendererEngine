#include <Tetragrama/EditorSession.h>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
namespace G = ZEngine::Core::Maths::Geometry;

namespace Tetragrama
{
    bool EditorSession::BeginLight(const Gizmo::LightBegin& input)
    {
        if (!Allowed() || !Editable() || Active() || !input.Key || !input.PointerSerial || !Gizmo::IsLightHandle(input.Constraint))
        {
            return false;
        }
        SynchronizeHierarchy();
        Gizmo::LightFrame  frame  = {};
        Gizmo::LightHandle handle = {};
        if (!Gizmo::ResolveLightFrame(*this, frame) || !Gizmo::SameLightFrame(frame, input.Expected) || !Gizmo::ResolveLightHandle(frame, input.Constraint, handle))
        {
            return Reject("The component handle no longer matches the selected light.");
        }
        const Vec3f  reference = std::abs(handle.Direction.y) < Gizmo::Constants::AxisReferenceParallelY ? Vec3f(0.f, 1.f, 0.f) : Vec3f(1.f, 0.f, 0.f);
        Vec3f        right     = cross3d(reference, handle.Direction);
        const double length    = std::hypot(double(right.x), double(right.y), double(right.z));
        if (!std::isfinite(length) || length <= 0.0)
        {
            return false;
        }
        right                         = {float(right.x / length), float(right.y / length), float(right.z / length)};
        const auto            up      = cross3d(handle.Direction, right);
        G::TranslationCapture capture = {.Projection = input.Projection, .Bounds = input.Bounds, .Pointer = input.Pointer, .Anchor = handle.Position, .AxisMinDenominator = Gizmo::Constants::AxisParallelDenominator, .PlaneMinCosine = Gizmo::Constants::PlaneParallelCosine, .MinimumBasisVolume = Gizmo::Constants::FrameAxesMinimumVolume, .Axes = 1};
        for (uint32_t row = 0; row < 3; ++row)
        {
            capture.Basis(row, 0) = handle.Direction[row];
            capture.Basis(row, 1) = right[row];
            capture.Basis(row, 2) = up[row];
        }
        Gizmo::LightInteraction interaction = {.Frame = frame, .HandleFrame = handle, .Snap = handle.Field == AuthoredFloatField::LightSpotAngle ? m_gizmo.RotationSnap : m_gizmo.TranslationSnap, .Key = input.Key, .PointerSerial = input.PointerSerial, .Constraint = input.Constraint};
        if (!G::CaptureTranslation(capture, &interaction.Mapping))
        {
            return Reject("The light handle is parallel to the captured view.");
        }
        if (!BeginField(frame.Target, handle.Field))
        {
            return false;
        }
        m_light = interaction;
        return true;
    }

    bool EditorSession::ValidateLight()
    {
        const auto* transform = m_scene->GetComponent<Components::TransformComponent>(m_field_entity);
        const auto* light     = m_scene->GetComponent<Components::LightComponent>(m_field_entity);
        if (!transform || !light || m_scene->Hierarchy().ParentWorldRevision(m_field_entity) != m_light.Frame.ParentWorldRevision || !SameLocalTransform(CaptureLocalTransform(*transform), m_light.Frame.Pose) || light->LightType != m_light.Frame.Light.Type)
        {
            return false;
        }
        // Only the captured field is ours. Other geometry changes invalidate the
        // mapping; unrelated intensity/color values remain owned by their author.
        return m_field.Field == AuthoredFloatField::LightRange ? light->SpotAngle == m_light.Frame.Light.SpotAngle : light->Range == m_light.Frame.Light.Range;
    }

    bool EditorSession::UpdateLight(const Vec2f& pointer, bool pointer_down, bool fine, bool invert_snap, bool focused, bool cancel, bool capture_lost)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!LightActive())
        {
            return true;
        }
        if (!focused || cancel || capture_lost)
        {
            Cancel();
            return true;
        }
        if (!ValidateActive())
        {
            Cancel();
            return Reject("The light drag was cancelled because its captured component changed.");
        }
        G::TranslationSample sample   = {};
        bool                 accepted = false;
        if (G::SampleTranslation(m_light.Mapping, pointer, fine ? Gizmo::Constants::FineMotionFactor : 1.f, false, 0.f, &sample))
        {
            double delta = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                delta += double(sample.Delta[axis]) * m_light.HandleFrame.Direction[axis];
            }
            double value = m_field.Before;
            if (delta != 0.0)
            {
                value = m_field.Field == AuthoredFloatField::LightRange ? double(m_field.Before) + delta : 2.0 * Gizmo::Constants::DegreesPerRadian * std::atan((double(m_light.HandleFrame.Parameter) + delta) / m_light.Frame.Light.Range);
                if (!fine && (m_light.Snap.Enabled != invert_snap))
                {
                    value = std::round(value / m_light.Snap.Increment) * m_light.Snap.Increment;
                }
            }
            if (std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max())
            {
                accepted = PreviewField(float(value));
            }
        }
        return !pointer_down && Active() ? Commit() : accepted;
    }

    bool EditorSession::ReadLightSnapshot(Gizmo::LightSnapshot& output)
    {
        if (!OnOwnerThread() || !LightActive() || m_hierarchy_pending || !ValidateActive())
        {
            return false;
        }
        Gizmo::LightSnapshot result = {.Value = m_field.After, .Key = m_light.Key, .PointerSerial = m_light.PointerSerial, .Constraint = m_light.Constraint};
        if (!Gizmo::ResolveLightFrame(*this, result.Frame))
        {
            return false;
        }
        output = result;
        return true;
    }
} // namespace Tetragrama
