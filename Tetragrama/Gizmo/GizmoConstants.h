#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <numbers>

namespace Tetragrama::Gizmo
{
    struct SnapPreset
    {
        float       Increment = 0.f;
        const char* Label     = "";
    };
} // namespace Tetragrama::Gizmo

namespace Tetragrama::Gizmo::Constants
{
    // Approved GZ2/GZ3/GZ5/GZ9/GZ10/GZ14 behavior. Core geometry accepts these
    // policies as parameters and never depends on editor/render/UI state.
    inline constexpr float    AxisLengthPx                   = 120.f; // GZ2
    inline constexpr float    AxisStartPx                    = 10.f;  // GZ2
    inline constexpr float    AxisIdleWidthPx                = 2.5f;  // GZ2
    inline constexpr float    AxisEmphasizedWidthPx          = 4.5f;  // GZ6
    inline constexpr float    GlowWidthPx                    = 9.f;   // GZ6
    inline constexpr float    GlowAlpha                      = 0.2f;  // GZ6
    inline constexpr float    ArrowLengthPx                  = 14.f;  // GZ2
    inline constexpr float    ArrowWidthPx                   = 11.f;  // GZ2
    inline constexpr float    CenterSizePx                   = 10.f;  // GZ2
    inline constexpr float    PlaneStartPx                   = 33.f;  // GZ2
    inline constexpr float    PlaneEndPx                     = 67.f;  // GZ2
    inline constexpr float    PlaneIdleAlpha                 = 0.2f;  // tile 73:1403
    inline constexpr float    PlaneEmphasizedAlpha           = 0.55f; // tile 73:1403
    inline constexpr float    PlaneIdleBorderAlpha           = 0.7f;  // tile 73:1403
    inline constexpr float    PlaneIdleBorderWidthPx         = 1.5f;  // tile 73:1403
    inline constexpr float    PlaneEmphasizedBorderWidthPx   = 2.f;   // tile 73:1403
    inline constexpr float    OtherHoveredAlpha              = 0.4f;  // GZ6
    inline constexpr float    OtherActiveAlpha               = 0.35f; // GZ6
    inline constexpr float    FrameAxesMinimumVolume         = 1e-6f; // reject singular frames; local axes may be skewed
    inline constexpr float    RingRadiusPx                   = 100.f; // GZ5
    inline constexpr float    ViewRingRadiusPx               = 110.f; // GZ5
    inline constexpr float    RingIdleWidthPx                = 2.f;   // GZ5
    inline constexpr float    RingEmphasizedWidthPx          = 3.5f;  // GZ6
    inline constexpr float    ViewRingIdleAlpha              = 0.5f;  // GZ5
    // The reference ring is composited over BgViewport. Its foreground remains
    // unchanged; this underlay preserves the same contrast over bright scene
    // content without changing that approved foreground metric.
    inline constexpr float    ViewRingContrastOutlineWidthPx = 4.f;
    inline constexpr float    RotationWedgeAlpha             = 0.28f; // tile 73:1565
    inline constexpr float    RotationWedgeWidthPx           = 1.5f;  // tile 73:1565
    inline constexpr unsigned RingSegments                   = 64;    // GZ5
    inline constexpr double   StrokeJoinMinimumLengthPx      = 1e-4;  // GZ5, reject collapsed projected chords
    inline constexpr double   StrokeJoinMinimumDirection     = 1e-4;  // GZ5, no bisector at a folded-back vertex
    inline constexpr float    GhostDashOnPx                  = 4.f;   // GZ13
    inline constexpr float    GhostDashOffPx                 = 3.f;   // GZ13
    inline constexpr float    GhostAlpha                     = 0.4f;  // tile 73:1333
    inline constexpr float    SelectionBoxWidthPx            = 1.5f;  // tile 73:1333
    inline constexpr float    GuideWidthPx                   = 1.f;   // Translate, §6.2
    inline constexpr float    GuideAlpha                     = 0.6f;  // Translate, §6.2
    inline constexpr float    GuideDashOnPx                  = 5.f;   // Translate, §6.2
    inline constexpr float    GuideDashOffPx                 = 4.f;   // Translate, §6.2
    inline constexpr float    PillCursorOffsetPx             = 16.f;  // GZ16
    inline constexpr float    PillBorderWidthPx              = 1.f;   // tile 73:1398
    inline constexpr int      PillValueDecimals              = 3;     // GZ16, raw metres
    inline constexpr float    ScaleCubeSizePx                = 12.f;  // GZ10
    inline constexpr float    ScaleEmphasizedCubeSizePx      = 16.f;  // scale hover/drag tiles
    inline constexpr float    ScaleUniformCubeSizePx         = 18.f;  // scale uniform tile
    inline constexpr float    ScaleUniformOutlinePx          = 1.5f;  // tile 73:1886
    inline constexpr float    ScaleDisabledAlpha             = 0.4f;  // GZ10
    inline constexpr float    ScaleGhostDashOnPx             = 3.f;   // scale handle ghost, §6.2
    inline constexpr float    ScaleGhostDashOffPx            = 2.f;   // scale handle ghost, §6.2
    inline constexpr float    PivotRingSizePx                = 20.f;  // GZ8
    inline constexpr float    PivotRingWidthPx               = 2.f;   // GZ8
    inline constexpr float    PivotCrossSizePx               = 32.f;  // GZ8
    inline constexpr float    PivotCrossWidthPx              = 1.5f;  // tile 74:1284
    inline constexpr float    PivotOriginSizePx              = 8.f;   // tile 74:1335
    inline constexpr float    PivotLinkWidthPx               = 1.f;   // tile 74:1334
    inline constexpr float    PivotLinkAlpha                 = 0.6f;  // GZ8
    inline constexpr float    ComponentLineWidthPx           = 1.5f;  // point/spot tiles
    inline constexpr float    ComponentHandleSizePx          = 9.f;   // light range/outer handles
    inline constexpr float    ComponentHandleEmphasizedPx    = 12.f;
    inline constexpr float    CameraHandleSizePx             = 8.f; // tile 74:1700
    inline constexpr unsigned CameraFovArcSegments           = 32;
    inline constexpr float    CameraFovArcFraction           = .25f;
    inline constexpr float    CameraMarkerWidthPx            = 32.f; // tile 74:1697
    inline constexpr float    CameraMarkerHeightPx           = 28.f;
    inline constexpr float    CameraLensLengthPx             = 14.f; // tile 74:1698
    inline constexpr float    CameraLensInnerHeightPx        = 16.f;
    inline constexpr float    CameraLensOuterHeightPx        = 32.f;
    inline constexpr float    CameraMarkerLineWidthPx        = 2.f;
    inline constexpr float    PointSunSizePx                 = 14.f; // tile 74:1527
    inline constexpr float    SpotSunSizePx                  = 12.f; // tile 74:1582
    inline constexpr float    DirectionalSunSizePx           = 18.f; // tile 74:1628
    inline constexpr float    LightSunRayGapPx               = 4.f;
    inline constexpr float    LightSunRayLengthPx            = 6.f;
    inline constexpr float    DirectionalArrowWorldLength    = 1.f;
    inline constexpr float    DirectionalRayWorldOffset      = .25f;
    inline constexpr float    DirectionalRayAlpha            = .55f;                     // tile 74:1596
    inline constexpr float    LineHitTolerancePx             = 8.f;                      // GZ4, arrows and rings
    inline constexpr float    BoxHitPaddingPx                = 4.f;                      // GZ4, cubes and centre
    inline constexpr float    PlaneHitPaddingPx              = 2.f;                      // GZ4
    inline constexpr float    AxisFadeStart                  = 0.95f;                    // GZ3
    inline constexpr float    AxisHide                       = 0.99f;                    // GZ3
    inline constexpr float    PlaneHide                      = 0.1f;                     // GZ3
    inline constexpr float    PlaneFadeEnd                   = 0.25f;                    // GZ3
    inline constexpr float    AxisParallelDenominator        = 1e-4f;                    // GZ13, architecture §5.4
    inline constexpr float    UnitAxisTolerance              = 1e-4f;                    // GZ4, component direction/frame validation
    inline constexpr float    AxisReferenceParallelY         = .9f;                      // GZ4, choose a stable auxiliary axis
    inline constexpr float    PlaneParallelCosine            = 1e-3f;                    // GZ13, architecture §5.4
    inline constexpr float    RotateTangentFallbackCosine    = 0.15f;                    // GZ5
    inline constexpr float    MinimumScaleRatio              = 0.001f;                   // GZ10
    inline constexpr float    MinimumEditableScale           = 1e-4f;                    // GZ10
    inline constexpr float    ParentLocalMaxError            = 1e-3f;                    // GZ10
    inline constexpr float    AxisScaleMinimumGrabFraction   = 0.25f;                    // GZ10, math §8.5
    inline constexpr float    UniformScalePixelsPerEfold     = 120.f;                    // GZ10, math §8.5
    inline constexpr float    NearDistanceMultiplier         = 2.f;                      // GZ14
    inline constexpr float    FineMotionFactor               = 0.1f;                     // GZ9
    inline constexpr double   RadiansPerDegree               = std::numbers::pi / 180.0; // GZ5
    inline constexpr double   HalfRadiansPerDegree           = std::numbers::pi / 360.0; // GZ5
    inline constexpr double   DegreesPerRadian               = 180.0 / std::numbers::pi; // GZ5
    inline constexpr float    DefaultTranslateSnap           = 0.1f;                     // GZ9, metres
    inline constexpr float    DefaultRotateSnapDegrees       = 10.f;                     // GZ9
    inline constexpr float    DefaultScaleSnap               = 0.25f;                    // GZ9
    inline constexpr float    SnapMenuWidthPx                = 160.f;                    // GZ9, toolbar board 271:3077
    inline constexpr auto     TranslationSnapPresets         = ZEngine::Core::Containers::FixedArray<SnapPreset, 6>{
        {
         {0.01f, "0.01"},
         {0.05f, "0.05"},
         {DefaultTranslateSnap, "0.1"},
         {0.25f, "0.25"},
         {0.5f, "0.5"},
         {1.f, "1"},
         }
    };
    inline constexpr auto RotationSnapPresets = ZEngine::Core::Containers::FixedArray<SnapPreset, 7>{
        {
         {1.f, "1\xC2\xB0"},
         {5.f, "5\xC2\xB0"},
         {DefaultRotateSnapDegrees, "10\xC2\xB0"},
         {15.f, "15\xC2\xB0"},
         {22.5f, "22.5\xC2\xB0"},
         {45.f, "45\xC2\xB0"},
         {90.f, "90\xC2\xB0"},
         }
    };
    inline constexpr auto ScaleSnapPresets = ZEngine::Core::Containers::FixedArray<SnapPreset, 5>{
        {
         {0.05f, "0.05"},
         {0.1f, "0.1"},
         {DefaultScaleSnap, "0.25"},
         {0.5f, "0.5"},
         {1.f, "1"},
         }
    };
} // namespace Tetragrama::Gizmo::Constants
