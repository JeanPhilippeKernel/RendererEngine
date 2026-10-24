#include <Tetragrama/Gizmo/GizmoConstants.h>
#include <Tetragrama/Gizmo/GizmoDebugFrame.h>
#include <ZEngine/Rendering/Renderers/GizmoPrimitiveBuilder.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUITokens.h>
#include <cmath>
#include <numbers>

using namespace ZEngine;
using namespace Core::Maths;
using namespace Rendering::Renderers;
namespace G = Core::Maths::Geometry;
namespace C = Tetragrama::Gizmo::Constants;

namespace Tetragrama::Gizmo
{
    bool BuildDebugFrame(const Rendering::Cameras::CameraFrameData& camera, float logical_height, uint32_t width, uint32_t height, GizmoFrameData& output)
    {
        output.Reset();
        if (camera.ProjectionType != Rendering::Cameras::CameraType::PERSPECTIVE && camera.ProjectionType != Rendering::Cameras::CameraType::ORTHOGRAPHIC)
        {
            return false;
        }
        G::ProjectionFrame frame = {};
        const auto         kind  = camera.ProjectionType == Rendering::Cameras::CameraType::PERSPECTIVE ? G::ProjectionKind::Perspective : G::ProjectionKind::Orthographic;
        if (!G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, kind, camera.UsesReverseZ, &frame))
        {
            return false;
        }
        GizmoPrimitiveBuilder builder(output, frame, logical_height, width, height);
        float                 world_per_pixel = 0.f;
        if (!builder.Valid() || !G::WorldPerPixel(frame, {}, logical_height, &world_per_pixel))
        {
            return false;
        }
        const UI::Tokens::Color tokens[3] = {UI::Tokens::AccentRed, UI::Tokens::AccentGreen, UI::Tokens::AccentBlue};
        const uint32_t          selection = UI::ZUIPackColor(UI::Tokens::AccentOrange.R, UI::Tokens::AccentOrange.G, UI::Tokens::AccentOrange.B, UI::Tokens::AccentOrange.A);
        const G::Viewport       viewport  = {
            {},
            {float(width), float(height)}
        };
        const float       pixel_scale = float(height) / logical_height;
        G::ProjectedPoint center      = {};
        if (!G::Project({}, frame, viewport, &center))
        {
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const auto&    token = tokens[axis];
            const uint32_t color = UI::ZUIPackColor(token.R, token.G, token.B, token.A);
            Vec3f          start = {}, tip = {};
            start[axis] = C::AxisStartPx * world_per_pixel;
            tip[axis]   = C::AxisLengthPx * world_per_pixel;
            (void) builder.Line({.A = start, .B = tip, .Width = C::AxisIdleWidthPx, .Color = color, .RoundCaps = true});
            G::ProjectedPoint projected = {};
            if (!G::Project(tip, frame, viewport, &projected))
            {
                continue;
            }
            const Vec2f  delta  = projected.Screen - center.Screen;
            const double length = std::hypot(double(delta.x), double(delta.y));
            if (length <= C::ArrowLengthPx * pixel_scale)
            {
                continue;
            }
            const Vec2f direction(float(delta.x / length), float(delta.y / length));
            const Vec2f across(-direction.y, direction.x);
            const Vec2f base = projected.Screen - direction * (C::ArrowLengthPx * pixel_scale);
            Vec3f       a = {}, b = {};
            if (G::UnprojectAtDepth(base + across * (C::ArrowWidthPx * 0.5f * pixel_scale), projected.Depth, frame, viewport, &a) && G::UnprojectAtDepth(base - across * (C::ArrowWidthPx * 0.5f * pixel_scale), projected.Depth, frame, viewport, &b))
            {
                (void) builder.Triangle({.A = tip, .B = a, .C = b, .Color = color});
            }
        }
        // A screen-facing dashed ring exercises phase continuity and pixel sizing.
        Vec3f right = {}, up = {};
        if (!G::UnprojectAtDepth(center.Screen + Vec2f(C::RingRadiusPx * pixel_scale, 0.f), center.Depth, frame, viewport, &right) || !G::UnprojectAtDepth(center.Screen + Vec2f(0.f, -C::RingRadiusPx * pixel_scale), center.Depth, frame, viewport, &up))
        {
            output.Reset();
            return false;
        }
        float             phase           = 0.f;
        Vec3f             previous        = right;
        G::ProjectedPoint previous_screen = {};
        if (!G::Project(previous, frame, viewport, &previous_screen))
        {
            output.Reset();
            return false;
        }
        for (uint32_t i = 1; i <= C::RingSegments; ++i)
        {
            const float angle = (2.f * std::numbers::pi_v<float> * i) / C::RingSegments;
            const Vec3f point = i == C::RingSegments ? right : right * std::cos(angle) + up * std::sin(angle);
            (void) builder.Line({.A = previous, .B = point, .Width = C::RingIdleWidthPx, .DashOn = C::GhostDashOnPx, .DashOff = C::GhostDashOffPx, .DashPhase = phase, .Color = selection});
            G::ProjectedPoint projected = {};
            if (G::Project(point, frame, viewport, &projected))
            {
                phase += float(std::hypot(double(projected.Screen.x) - previous_screen.Screen.x, double(projected.Screen.y) - previous_screen.Screen.y)) / pixel_scale;
            }
            previous        = point;
            previous_screen = projected;
        }
        (void) builder.Sprite({.Size = C::ScaleCubeSizePx, .Fill = selection});
        return !output.Empty();
    }
} // namespace Tetragrama::Gizmo
