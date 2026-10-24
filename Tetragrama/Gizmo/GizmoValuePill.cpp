#include <Tetragrama/Gizmo/GizmoAppearance.h>
#include <Tetragrama/Gizmo/GizmoCamera.h>
#include <Tetragrama/Gizmo/GizmoLight.h>
#include <Tetragrama/Gizmo/GizmoValuePill.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine;
using namespace Core::Maths;

namespace Tetragrama::Gizmo
{
    bool FormatTranslationPill(Handle constraint, const Vec3f& components, ValuePillText& output, AxisLabels labels)
    {
        if (labels != AxisLabels::XYZ && labels != AxisLabels::UNV)
        {
            return false;
        }
        const char* label = nullptr;
        switch (constraint)
        {
            case Handle::TranslateX:
                label = "X";
                break;
            case Handle::TranslateY:
                label = "Y";
                break;
            case Handle::TranslateZ:
                label = "Z";
                break;
            case Handle::TranslateYZ:
                label = "YZ";
                break;
            case Handle::TranslateZX:
                label = "ZX";
                break;
            case Handle::TranslateXY:
                label = "XY";
                break;
            case Handle::TranslateView:
                label = "XYZ";
                break;
            default:
                return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(components[axis]))
            {
                return false;
            }
        }
        ValuePillText result = {};
        char*         cursor = result.Values.data();
        char* const   end    = result.Values.data() + result.Values.size() - 1;
        uint32_t      count  = 0;
        for (; label[count]; ++count)
        {
            const uint32_t axis = uint32_t(label[count] - 'X');
            if (count)
            {
                *cursor++ = ' ';
            }
            result.Label[count]   = labels == AxisLabels::UNV ? "UNV"[axis] : label[count];
            char* const sign      = cursor++;
            const auto  converted = std::to_chars(cursor, end, std::abs(components[axis]), std::chars_format::fixed, Constants::PillValueDecimals);
            if (converted.ec != std::errc{})
            {
                return false;
            }
            bool nonzero = false;
            for (const char* digit = cursor; digit != converted.ptr; ++digit)
            {
                nonzero |= *digit >= '1' && *digit <= '9';
            }
            *sign  = components[axis] < 0.f && nonzero ? ZUI_GLYPH_MINUS[0] : '+';
            cursor = converted.ptr;
        }
        *cursor = '\0';
        output  = result;
        return true;
    }

    bool FormatRotationPill(Handle constraint, double radians, ValuePillText& output, AxisLabels labels)
    {
        if (labels != AxisLabels::XYZ && labels != AxisLabels::UNV)
        {
            return false;
        }
        const char* label = nullptr;
        switch (constraint)
        {
            case Handle::RotateX:
                label = "X";
                break;
            case Handle::RotateY:
                label = "Y";
                break;
            case Handle::RotateZ:
                label = "Z";
                break;
            case Handle::RotateView:
                label = "View";
                break;
            default:
                return false;
        }
        const double degrees = radians * Constants::DegreesPerRadian;
        if (!std::isfinite(degrees))
        {
            return false;
        }
        ValuePillText result = {};
        for (uint32_t i = 0; label[i]; ++i)
        {
            result.Label[i] = label[i];
        }
        if (labels == AxisLabels::UNV && constraint != Handle::RotateView)
        {
            result.Label[0] = "UNV"[result.Label[0] - 'X'];
        }
        char* const begin     = result.Values.data();
        // Leave a leading sign byte and two trailing bytes for degree/NUL.
        const auto  converted = std::to_chars(begin + 1, begin + result.Values.size() - 2, std::abs(degrees), std::chars_format::fixed, 1);
        if (converted.ec != std::errc{})
        {
            return false;
        }
        bool nonzero = false;
        for (const char* digit = begin + 1; digit != converted.ptr; ++digit)
        {
            nonzero |= *digit >= '1' && *digit <= '9';
        }
        char* end = converted.ptr;
        if (radians < 0.0 && nonzero)
        {
            *begin = ZUI_GLYPH_MINUS[0];
        }
        else
        {
            std::copy(begin + 1, end, begin);
            --end;
        }
        *end++ = ZUI_GLYPH_DEGREE[0];
        *end   = '\0';
        output = result;
        return true;
    }

    bool FormatScalePill(Handle constraint, float ratio, ValuePillText& output)
    {
        if (constraint < Handle::ScaleX || constraint > Handle::ScaleUniform || !std::isfinite(ratio) || ratio < Constants::MinimumScaleRatio)
        {
            return false;
        }
        ValuePillText result = {};
        if (constraint == Handle::ScaleUniform)
        {
            result.Label[0] = 'X';
            result.Label[1] = 'Y';
            result.Label[2] = 'Z';
        }
        else
        {
            result.Label[0] = char('X' + uint32_t(constraint) - uint32_t(Handle::ScaleX));
        }
        result.Values[0]     = ZUI_GLYPH_MULTIPLY[0];
        const auto converted = std::to_chars(result.Values.data() + 1, result.Values.data() + result.Values.size() - 1, ratio, std::chars_format::fixed, 2);
        if (converted.ec != std::errc{})
        {
            return false;
        }
        *converted.ptr = '\0';
        output         = result;
        return true;
    }

    bool FormatLightPill(Handle constraint, float value, ValuePillText& output)
    {
        const bool angle = constraint == Handle::SpotOuterAngle;
        if (!IsLightHandle(constraint) || !std::isfinite(value) || value <= 0.f || (angle && value >= 180.f))
        {
            return false;
        }
        ValuePillText result = {};
        if (Helpers::secure_strcpy(result.Label.data(), result.Label.size(), angle ? "Outer" : "Range") != 0)
        {
            return false;
        }
        const auto converted = std::to_chars(result.Values.data(), result.Values.data() + result.Values.size() - 2, value, std::chars_format::fixed, angle ? 1 : Constants::PillValueDecimals);
        if (converted.ec != std::errc{})
        {
            return false;
        }
        char* end = converted.ptr;
        if (angle)
        {
            *end++ = ZUI_GLYPH_DEGREE[0];
        }
        *end   = '\0';
        output = result;
        return true;
    }

    bool FormatCameraPill(Handle constraint, float value, ValuePillText& output)
    {
        const bool angle = constraint == Handle::CameraFovUpperLeft || constraint == Handle::CameraFovUpperRight;
        if (!IsCameraHandle(constraint) || !std::isfinite(value) || value <= 0.f || (angle && value >= 180.f))
        {
            return false;
        }
        ValuePillText result = {};
        const bool    near   = constraint == Handle::CameraNearLeft || constraint == Handle::CameraNearRight;
        if (Helpers::secure_strcpy(result.Label.data(), result.Label.size(), angle ? "FOV" : near ? "Near" : "Far") != 0)
        {
            return false;
        }
        const auto converted = std::to_chars(result.Values.data(), result.Values.data() + result.Values.size() - 2, value, std::chars_format::fixed, angle ? 1 : Constants::PillValueDecimals);
        if (converted.ec != std::errc{})
        {
            return false;
        }
        char* end = converted.ptr;
        if (angle)
        {
            *end++ = ZUI_GLYPH_DEGREE[0];
        }
        *end   = '\0';
        output = result;
        return true;
    }

    static bool DrawValuePill(UI::ZUIContext* ctx, const Geometry::Viewport& viewport, const Vec2f& pointer, Handle constraint, const ValuePillText& text)
    {
        using namespace UI;
        if (!ctx || !ctx->Root)
        {
            return false;
        }
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            if (!std::isfinite(viewport.Origin[axis]) || !std::isfinite(viewport.Size[axis]) || !std::isfinite(pointer[axis]) || viewport.Size[axis] <= 0.f || std::abs(double(viewport.Origin[axis]) + viewport.Size[axis]) > std::numeric_limits<float>::max())
            {
                return false;
            }
        }
        const auto* heading = ctx->GetFont(ZUIFontSize::Heading);
        const auto* body    = ctx->GetFont(ZUIFontSize::Body);
        if (!heading || !heading->Glyphs || !body || !body->Glyphs)
        {
            return false;
        }
        float label[2] = {}, values[2] = {};
        ZUIMeasureText(heading, text.Label.data(), uint32_t(Helpers::secure_strlen(text.Label.data())), label);
        ZUIMeasureText(body, text.Values.data(), uint32_t(Helpers::secure_strlen(text.Values.data())), values);
        const float fixed = Metrics::Space8 * 2.f + label[0] + Metrics::Space6;
        const float width = std::min(fixed + values[0], viewport.Size.x);
        if (!std::isfinite(width) || !std::isfinite(label[1]) || !std::isfinite(values[1]) || width < fixed + Metrics::Space8 || viewport.Size.y < Metrics::HeightField)
        {
            return false;
        }
        const Vec2f origin(float(std::clamp(double(pointer.x) + Constants::PillCursorOffsetPx, double(viewport.Origin.x), double(viewport.Origin.x) + viewport.Size.x - width)), float(std::clamp(double(pointer.y) + Constants::PillCursorOffsetPx, double(viewport.Origin.y), double(viewport.Origin.y) + viewport.Size.y - Metrics::HeightField)));
        auto*       saved = ZUIBeginForeground(ctx);
        auto*       pill  = ZUIBeginRow(ctx, "##gizmo_value_pill", ZPx(width), ZPx(Metrics::HeightField));
        pill->Flags       = pill->Flags | ZUI_FloatX | ZUI_FloatY | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_ClipChildren;
        pill->FloatPos[0] = origin.x;
        pill->FloatPos[1] = origin.y;
        pill->Padding[0] = pill->Padding[2] = Metrics::Space8;
        pill->Gap                           = Metrics::Space6;
        pill->CrossAlign                    = ZUIAlign::Center;
        pill->BorderThickness               = Constants::PillBorderWidthPx;
        ZUIBoxSetCornerRadius(pill, Metrics::Radius3);
        constexpr float background[4] = ZUI_TOKEN(Tokens::BgWindow);
        constexpr float border[4]     = ZUI_TOKEN(Tokens::BorderField);
        constexpr float primary[4]    = ZUI_TOKEN(Tokens::TextPrimary);
        const auto      token         = ConstraintColor(constraint);
        const float     accent[4]     = {token.R, token.G, token.B, token.A};
        ZUIBoxSetColorArr(pill, background);
        Helpers::secure_memcpy(pill->BorderColor, sizeof(pill->BorderColor), border, sizeof(border));
        ZUILabelTight(ctx, text.Label.data(), accent, ZUIFontSize::Heading, "##gizmo_value_axis");
        ZUILabelEllipsized(ctx, text.Values.data(), width - fixed, primary, ZUIFontSize::Body, "##gizmo_value_number");
        ZUIEndRow(ctx);
        ZUIEndForeground(ctx, saved);
        return true;
    }

    bool DrawTranslationPill(UI::ZUIContext* ctx, const Geometry::Viewport& viewport, const Vec2f& pointer, Handle constraint, const Vec3f& components, AxisLabels labels)
    {
        ValuePillText text = {};
        return FormatTranslationPill(constraint, components, text, labels) && DrawValuePill(ctx, viewport, pointer, constraint, text);
    }

    bool DrawRotationPill(UI::ZUIContext* ctx, const Geometry::Viewport& viewport, const Vec2f& pointer, Handle constraint, double radians, AxisLabels labels)
    {
        ValuePillText text = {};
        return FormatRotationPill(constraint, radians, text, labels) && DrawValuePill(ctx, viewport, pointer, constraint, text);
    }

    bool DrawScalePill(UI::ZUIContext* ctx, const Geometry::Viewport& viewport, const Vec2f& pointer, Handle constraint, float ratio)
    {
        ValuePillText text = {};
        return FormatScalePill(constraint, ratio, text) && DrawValuePill(ctx, viewport, pointer, constraint, text);
    }

    bool DrawPivotPill(UI::ZUIContext* ctx, const Geometry::Viewport& viewport, const Vec2f& pointer)
    {
        ValuePillText text = {};
        return Helpers::secure_strcpy(text.Label.data(), text.Label.size(), "Pivot") == 0 && Helpers::secure_strcpy(text.Values.data(), text.Values.size(), "Alt + drag") == 0 && DrawValuePill(ctx, viewport, pointer, Handle::PivotEdit, text);
    }

    bool DrawLightPill(UI::ZUIContext* ctx, const Geometry::Viewport& viewport, const Vec2f& pointer, Handle constraint, float value)
    {
        ValuePillText text = {};
        return FormatLightPill(constraint, value, text) && DrawValuePill(ctx, viewport, pointer, constraint, text);
    }
    bool DrawCameraPill(UI::ZUIContext* ctx, const Geometry::Viewport& viewport, const Vec2f& pointer, Handle constraint, float value)
    {
        ValuePillText text = {};
        return FormatCameraPill(constraint, value, text) && DrawValuePill(ctx, viewport, pointer, constraint, text);
    }
} // namespace Tetragrama::Gizmo
