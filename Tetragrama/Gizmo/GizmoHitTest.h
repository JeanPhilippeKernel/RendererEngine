#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <type_traits>

namespace Tetragrama::Gizmo
{
    /// GZ4 order, independent of paint order. One handle may have many shapes
    /// (e.g. the front-half segments of a ring or an arrow shaft and head).
    enum class HitPriority : uint8_t
    {
        Center,
        ScaleCube,
        Plane,
        Arrowhead,
        AxisLine,
        AxisRing,
        ViewRing,
        Component,
    };

    enum class HitGeometry : uint8_t
    {
        Segment,
        Rectangle,
        ConvexQuad,
        Triangle,
    };

    /// Value-only logical-screen geometry. Rectangle uses min/max in Points[0/1];
    /// Segment uses its endpoints; ConvexQuad/Triangle use perimeter-ordered points.
    /// Depth is positive camera distance, not normalized/reverse-Z buffer depth.
    /// Padding is the GZ4 centreline tolerance or outward shape padding. The
    /// frame builder omits hidden, clipped-away and back-half ring shapes.
    struct HitShape
    {
        uint32_t                                                              Handle   = 0; // zero is never hittable
        HitPriority                                                           Priority = HitPriority::Component;
        HitGeometry                                                           Geometry = HitGeometry::Segment;
        float                                                                 Depth    = 0.f;
        float                                                                 Padding  = 0.f;
        ZEngine::Core::Containers::FixedArray<ZEngine::Core::Maths::Vec2f, 4> Points   = {};
    };

    struct HitResult
    {
        uint32_t    Handle   = 0;
        HitPriority Priority = HitPriority::Component;
        float       Depth    = 0.f;
    };
    static_assert(std::is_trivially_copyable_v<HitShape> && std::is_trivially_copyable_v<HitResult>);

    /// Allocation-free O(shape count), O(1) extra space. Invalid input does not
    /// capture. Among matching shapes, active_handle wins, then GZ4 priority,
    /// then camera depth. Exact ties use the smaller stable handle ID. Session
    /// capture, not hit testing, owns active drags outside the viewport/geometry.
    [[nodiscard]] HitResult HitTest(ZEngine::Core::Containers::ArrayView<const HitShape> shapes, const ZEngine::Core::Maths::Vec2f& point, uint32_t active_handle = 0);
} // namespace Tetragrama::Gizmo
