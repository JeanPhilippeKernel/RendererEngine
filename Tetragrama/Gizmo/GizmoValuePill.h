#pragma once
#include <Tetragrama/Gizmo/GizmoState.h>
#include <ZEngine/Core/Maths/Geometry.h>

namespace ZEngine::UI
{
    struct ZUIContext;
}

namespace Tetragrama::Gizmo
{
    enum class AxisLabels : uint8_t
    {
        XYZ,
        UNV,
    };

    struct ValuePillText
    {
        ZEngine::Core::Containers::FixedArray<char, 6>   Label  = {};
        // Three signed FLT_MAX values in fixed notation, three decimals, spaces
        // and terminator all fit. No locale, heap allocation or truncation.
        ZEngine::Core::Containers::FixedArray<char, 160> Values = {};
    };

    [[nodiscard]] bool FormatTranslationPill(Handle constraint, const ZEngine::Core::Maths::Vec3f& components, ValuePillText& output, AxisLabels labels = AxisLabels::XYZ);
    [[nodiscard]] bool FormatRotationPill(Handle constraint, double radians, ValuePillText& output, AxisLabels labels = AxisLabels::XYZ);
    [[nodiscard]] bool FormatScalePill(Handle constraint, float ratio, ValuePillText& output);
    [[nodiscard]] bool FormatLightPill(Handle constraint, float value, ValuePillText& output);
    [[nodiscard]] bool FormatCameraPill(Handle constraint, float value, ValuePillText& output);
    /// Noninteractive foreground ZUI presentation. Reuses theme tokens/fonts and
    /// frame-arena text/boxes; hides when even the label/field cannot fit.
    [[nodiscard]] bool DrawTranslationPill(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::Maths::Geometry::Viewport& viewport, const ZEngine::Core::Maths::Vec2f& pointer, Handle constraint, const ZEngine::Core::Maths::Vec3f& components, AxisLabels labels = AxisLabels::XYZ);
    [[nodiscard]] bool DrawRotationPill(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::Maths::Geometry::Viewport& viewport, const ZEngine::Core::Maths::Vec2f& pointer, Handle constraint, double radians, AxisLabels labels = AxisLabels::XYZ);
    [[nodiscard]] bool DrawScalePill(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::Maths::Geometry::Viewport& viewport, const ZEngine::Core::Maths::Vec2f& pointer, Handle constraint, float ratio);
    [[nodiscard]] bool DrawPivotPill(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::Maths::Geometry::Viewport& viewport, const ZEngine::Core::Maths::Vec2f& pointer);
    [[nodiscard]] bool DrawLightPill(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::Maths::Geometry::Viewport& viewport, const ZEngine::Core::Maths::Vec2f& pointer, Handle constraint, float value);
    [[nodiscard]] bool DrawCameraPill(ZEngine::UI::ZUIContext* ctx, const ZEngine::Core::Maths::Geometry::Viewport& viewport, const ZEngine::Core::Maths::Vec2f& pointer, Handle constraint, float value);
} // namespace Tetragrama::Gizmo
