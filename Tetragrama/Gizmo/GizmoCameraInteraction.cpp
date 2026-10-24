#include <Tetragrama/EditorSession.h>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
namespace G = ZEngine::Core::Maths::Geometry;

namespace Tetragrama
{
    bool EditorSession::BeginCamera(const Gizmo::CameraBegin& input)
    {
        if (!Allowed() || !Editable() || Active() || !input.Key || !input.PointerSerial || !Gizmo::IsCameraHandle(input.Constraint))
        {
            return false;
        }
        SynchronizeHierarchy();
        Gizmo::CameraFrame  frame  = {};
        Gizmo::CameraHandle handle = {};
        if (!Gizmo::ResolveCameraFrame(*this, frame) || !Gizmo::SameCameraFrame(frame, input.Expected) || !Gizmo::ResolveCameraHandle(frame, input.Constraint, handle))
        {
            return Reject("The component handle no longer matches the selected camera.");
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
        Gizmo::CameraInteraction interaction = {.Frame = frame, .HandleFrame = handle, .Snap = handle.Field == AuthoredFloatField::CameraFovY ? m_gizmo.RotationSnap : m_gizmo.TranslationSnap, .Key = input.Key, .PointerSerial = input.PointerSerial, .Constraint = input.Constraint};
        if (!G::CaptureTranslation(capture, &interaction.Mapping))
        {
            return Reject("The camera handle is parallel to the captured view.");
        }
        if (!BeginField(frame.Target, handle.Field))
        {
            return false;
        }
        m_camera_gizmo = interaction;
        return true;
    }
    bool EditorSession::ValidateCamera()
    {
        const auto* transform = m_scene->GetComponent<Components::TransformComponent>(m_field_entity);
        const auto* camera    = m_scene->GetComponent<Components::CameraComponent>(m_field_entity);
        if (!transform || !camera || m_scene->Hierarchy().ParentWorldRevision(m_field_entity) != m_camera_gizmo.Frame.ParentWorldRevision || !SameLocalTransform(CaptureLocalTransform(*transform), m_camera_gizmo.Frame.Pose) || camera->AspectRatio != m_camera_gizmo.Frame.Camera.AspectRatio)
        {
            return false;
        }
        // IsMain belongs to its own author. Geometry fields other than the one
        // we own must still match the captured mapping.
        if (m_field.Field == AuthoredFloatField::CameraFovY)
        {
            return camera->Near == m_camera_gizmo.Frame.Camera.Near && camera->Far == m_camera_gizmo.Frame.Camera.Far;
        }
        if (m_field.Field == AuthoredFloatField::CameraNear)
        {
            return camera->FovY == m_camera_gizmo.Frame.Camera.FovY && camera->Far == m_camera_gizmo.Frame.Camera.Far;
        }
        return camera->Near == m_camera_gizmo.Frame.Camera.Near && camera->FovY == m_camera_gizmo.Frame.Camera.FovY;
    }
    bool EditorSession::UpdateCamera(const Vec2f& pointer, bool pointer_down, bool fine, bool invert_snap, bool focused, bool cancel, bool capture_lost)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!CameraActive())
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
            return Reject("The camera drag was cancelled because its captured component changed.");
        }
        G::TranslationSample sample   = {};
        bool                 accepted = false;
        if (G::SampleTranslation(m_camera_gizmo.Mapping, pointer, fine ? Gizmo::Constants::FineMotionFactor : 1.f, false, 0.f, &sample))
        {
            double delta = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                delta += double(sample.Delta[axis]) * m_camera_gizmo.HandleFrame.Direction[axis];
            }
            double value = m_field.Before;
            if (delta != 0.0)
            {
                value = m_field.Field == AuthoredFloatField::CameraFovY ? 2.0 * Gizmo::Constants::DegreesPerRadian * std::atan((m_camera_gizmo.HandleFrame.Parameter + delta) / m_camera_gizmo.HandleFrame.Factor) : double(m_field.Before) + delta / m_camera_gizmo.HandleFrame.Factor;
                if (!fine && (m_camera_gizmo.Snap.Enabled != invert_snap))
                {
                    value = std::round(value / m_camera_gizmo.Snap.Increment) * m_camera_gizmo.Snap.Increment;
                }
            }
            if (std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max())
            {
                accepted = PreviewField(float(value));
            }
        }
        return !pointer_down && Active() ? Commit() : accepted;
    }
    bool EditorSession::ReadCameraSnapshot(Gizmo::CameraSnapshot& output)
    {
        if (!OnOwnerThread() || !CameraActive() || m_hierarchy_pending || !ValidateActive())
        {
            return false;
        }
        Gizmo::CameraSnapshot result = {.Value = m_field.After, .Key = m_camera_gizmo.Key, .PointerSerial = m_camera_gizmo.PointerSerial, .Constraint = m_camera_gizmo.Constraint};
        if (!Gizmo::ResolveCameraFrame(*this, result.Frame))
        {
            return false;
        }
        output = result;
        return true;
    }
} // namespace Tetragrama
