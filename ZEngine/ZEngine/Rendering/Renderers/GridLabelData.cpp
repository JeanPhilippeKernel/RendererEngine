#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Rendering/Renderers/GridLabelData.h>
#include <cmath>
#include <limits>

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        namespace G = Core::Maths::Geometry;
        using Core::Maths::Vec3f;

        bool UnitRange(float value)
        {
            return std::isfinite(value) && value >= 0.f && value <= 1.f;
        }

        bool Float(double value, float& output)
        {
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                return false;
            output = float(value);
            return true;
        }

        bool ValidGlyph(const GridLabelGlyph& glyph)
        {
            for (float uv : glyph.UV)
                if (!UnitRange(uv))
                    return false;
            return glyph.UV[2] > glyph.UV[0] && glyph.UV[3] > glyph.UV[1] && std::isfinite(glyph.Size.x) && std::isfinite(glyph.Size.y) && glyph.Size.x > 0.f && glyph.Size.y > 0.f;
        }

        double Distance(const Vec3f& point, const G::ProjectionFrame& camera)
        {
            double distance = 0.0;
            for (uint32_t i = 0; i < 3; ++i)
                distance += (double(point[i]) - camera.Position[i]) * camera.Forward[i];
            return distance;
        }
    } // namespace

    bool ValidGridLabelFrame(const GridLabelFrameData& frame)
    {
        if (frame.Count > 2 || (frame.Count && !frame.Atlas.Valid()))
            return false;
        if (frame.Push.Depths[2] != 0.f && frame.Push.Depths[2] != 1.f)
            return false;
        if (frame.Push.Depths[3] != 0.f)
            return false;
        for (uint32_t i = 0; i < frame.Count; ++i)
        {
            const auto& rect = frame.Push.Rects[i];
            for (uint32_t lane = 0; lane < 4; ++lane)
                if (!std::isfinite(rect[lane]) || !UnitRange(frame.Push.UVs[i][lane]) || !UnitRange(frame.Push.Colors[i][lane]))
                    return false;
            if (rect[2] <= 0.f || rect[3] <= 0.f || !std::isfinite(rect[0] + rect[2]) || !std::isfinite(rect[1] + rect[3]) || frame.Push.UVs[i][2] <= frame.Push.UVs[i][0] || frame.Push.UVs[i][3] <= frame.Push.UVs[i][1] || !UnitRange(frame.Push.Depths[i]))
                return false;
        }
        return true;
    }

    bool BuildGridLabelFrame(const Scenes::SceneGridRenderSnapshot& grid, const Cameras::CameraFrameData& camera, const GridLabelFontSnapshot& font, uint32_t width, uint32_t height, GridLabelFrameData& output)
    {
        ECS::SceneGridSettings settings = {};
        if (!Scenes::ReadSceneGridRenderSnapshot(grid, settings))
            return false;
        GridLabelFrameData frame = {};
        if (!settings.Enabled || settings.Labels == ECS::SceneGridLabels::None || !font.Atlas.Valid() || !width || !height)
        {
            output = frame;
            return true;
        }
        if (!std::isfinite(font.LogicalViewportHeight) || font.LogicalViewportHeight <= 0.f || !ValidGlyph(font.Glyphs[0]) || !ValidGlyph(font.Glyphs[1]))
            return false;
        const bool orthographic = camera.ProjectionType == Cameras::CameraType::ORTHOGRAPHIC;
        if (!orthographic && camera.ProjectionType != Cameras::CameraType::PERSPECTIVE)
            return false;
        G::ProjectionFrame projection = {};
        if (!G::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, orthographic ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, camera.UsesReverseZ, &projection))
            return false;
        const double near_z = camera.UsesReverseZ ? double(camera.Projection(3, 2)) - camera.Projection(2, 2) : camera.Projection(2, 2);
        const double near_w = camera.UsesReverseZ ? double(camera.Projection(3, 3)) - camera.Projection(2, 3) : camera.Projection(2, 3);
        if (near_z == 0.0 || !std::isfinite(near_w / near_z) || near_w / near_z <= 0.0)
            return false;
        constexpr double NearDistanceMultiplier = 2.0;
        const double     minimum_distance       = NearDistanceMultiplier * near_w / near_z;
        float            world_per_pixel        = 0.f;
        if (Distance(settings.Origin, projection) < minimum_distance)
        {
            output = frame;
            return true;
        }
        if (!G::WorldPerPixel(projection, settings.Origin, font.LogicalViewportHeight, &world_per_pixel))
            return false;
        const auto                                   rotation = Core::Maths::quaternionToMat4(settings.Orientation);
        const Vec3f                                  u        = {rotation(0, 0), rotation(1, 0), rotation(2, 0)};
        const Vec3f                                  v        = {rotation(0, 2), rotation(1, 2), rotation(2, 2)};
        const Core::Containers::FixedArray<Vec3f, 2> axes     = {u, v};
        const double                                 scale    = double(height) / font.LogicalViewportHeight;
        const G::Viewport                            viewport = {
            .Size = {float(width), float(height)}
        };
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            Vec3f anchor = {};
            bool  finite = true;
            for (uint32_t i = 0; i < 3; ++i)
            {
                const double value = double(settings.Origin[i]) + double(axes[axis][i]) * world_per_pixel * GridLabelAxisOffset;
                if (!Float(value, anchor[i]))
                {
                    finite = false;
                    break;
                }
            }
            G::ProjectedPoint point = {};
            if (!finite || Distance(anchor, projection) < minimum_distance || !G::Project(anchor, projection, viewport, &point) || !UnitRange(point.Depth))
                continue;
            // Match the Grid's radial fade in its plane, not world XZ or full
            // camera distance. Double intermediates keep valid extremes defined.
            double du = 0.0, dv = 0.0;
            for (uint32_t i = 0; i < 3; ++i)
            {
                const double offset  = double(anchor[i]) - camera.Position[i];
                du                  += offset * u[i];
                dv                  += offset * v[i];
            }
            const double relative = std::hypot(du, dv) / settings.FadeRadius;
            const auto&  color    = axis == 0 ? settings.UColor : settings.VColor;
            const double alpha    = relative < 1.0 ? color[3] * std::pow(1.0 - relative, settings.FadeStrength) : 0.0;
            if (alpha <= .001)
                continue;
            const auto&  glyph = font.Glyphs[axis];
            const double w = glyph.Size.x * scale, h = glyph.Size.y * scale;
            const double x = double(point.Screen.x) - w * .5, y = double(point.Screen.y) - h * .5;
            if (x >= width || y >= height || x + w <= 0.0 || y + h <= 0.0)
                continue; // clip offscreen labels; never clamp to the viewport
            const uint32_t                                index = frame.Count++;
            const Core::Containers::FixedArray<double, 4> rect  = {2.0 * x / width - 1.0, 2.0 * y / height - 1.0, 2.0 * w / width, 2.0 * h / height};
            for (uint32_t i = 0; i < rect.size(); ++i)
                if (!Float(rect[i], frame.Push.Rects[index][i]))
                    return false;
            frame.Push.UVs[index]    = glyph.UV;
            frame.Push.Colors[index] = {color[0], color[1], color[2], float(alpha)};
            frame.Push.Depths[index] = point.Depth;
        }
        if (frame.Count)
        {
            frame.Atlas          = font.Atlas;
            frame.Push.Depths[2] = camera.UsesReverseZ ? 1.f : 0.f;
        }
        if (!ValidGridLabelFrame(frame))
            return false;
        output = frame;
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
