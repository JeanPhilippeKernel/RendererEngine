#include <Tetragrama/Gizmo/GizmoAppearance.h>
#include <Tetragrama/Gizmo/GizmoConstants.h>
#include <Tetragrama/Gizmo/GizmoFrameBuilder.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUITokens.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Rendering::Renderers;
namespace G  = ZEngine::Core::Maths::Geometry;
namespace C  = Tetragrama::Gizmo::Constants;
namespace UI = ZEngine::UI;

namespace
{
    FrameInput Input(bool ortho = false, bool reverse = false, float distance = 10.f, uint32_t height = 600, float near_distance = 0.1f)
    {
        CameraSetting settings = {};
        settings.NearPlane     = near_distance;
        settings.FarPlane      = 1000.f;
        FlyCamera camera(4.f / 3.f, settings);
        camera.SetPosition({0.f, 0.f, distance});
        camera.SetOrientation(0.f, 0.f);
        if (ortho)
        {
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        }
        FrameInput input          = {};
        input.Camera              = camera.CaptureFrameData();
        input.Camera.UsesReverseZ = reverse;
        if (reverse)
        {
            for (uint32_t i = 0; i < 4; ++i)
            {
                input.Camera.Projection(2, i) = input.Camera.Projection(3, i) - input.Camera.Projection(2, i);
            }
        }
        input.Viewport = {
            { 37.f,  61.f},
            {800.f, 600.f}
        };
        input.TargetWidth  = height * 4 / 3;
        input.TargetHeight = height;
        input.Mode         = Operation::Translate;
        input.Editable     = true;
        return input;
    }

    G::ProjectionFrame Projection(const FrameInput& input)
    {
        G::ProjectionFrame frame = {};
        EXPECT_TRUE(G::MakeProjectionFrame(input.Camera.View, input.Camera.Projection, input.Camera.Position, input.Camera.ProjectionType == CameraType::ORTHOGRAPHIC ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, input.Camera.UsesReverseZ, &frame));
        return frame;
    }

    Vec2f Screen(const FrameInput& input, const Vec3f& point)
    {
        G::ProjectedPoint projected = {};
        EXPECT_TRUE(G::Project(point, Projection(input), input.Viewport, &projected));
        return projected.Screen;
    }

    const HitShape* Find(const HitFrame& hits, Handle handle, HitPriority priority)
    {
        for (const auto& shape : hits.View())
        {
            if (shape.Handle == uint32_t(handle) && shape.Priority == priority)
            {
                return &shape;
            }
        }
        return nullptr;
    }

    void ExpectCleared(const GizmoFrameData& output, const HitFrame& hits)
    {
        EXPECT_TRUE(output.Empty());
        EXPECT_EQ(hits.Count, 0u);
        EXPECT_EQ(hits.Pick({437.f, 361.f}).Handle, 0u);
    }

    uint32_t Packed(const UI::Tokens::Color& color, float alpha = 1.f)
    {
        return UI::ZUIPackColor(color.R, color.G, color.B, color.A * alpha);
    }
} // namespace

TEST(GizmoFrameBuilderTest, TranslateMeshDragEmitsGuideAndTwoOrientedBoxesWithoutNewHitShapes)
{
    auto input           = Input();
    input.Active         = Handle::TranslateX;
    input.Pivot          = {1.f, 0.f, 0.f};
    GizmoFrameData plain = {}, decorated = {};
    HitFrame       plain_hits = {}, decorated_hits = {};
    ASSERT_EQ(BuildFrame(input, plain, plain_hits), FrameBuildResult::Built);
    input.Drag.Visible = input.Drag.HasBox = true;
    input.Drag.StartPivot                  = {};
    input.Drag.MeshUUID                    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto start                       = ComposeTransformMatrix({0.f, 0.f, 0.f}, {.3f, -.5f, .7f}, {-2.f, 3.f, .5f});
    auto       current                     = start;
    current(0, 3)                          = 1.f;
    ASSERT_TRUE(G::TransformBox({-1.f, -2.f, -3.f}, {1.f, 2.f, 3.f}, start, &input.Drag.Ghost));
    ASSERT_TRUE(G::TransformBox({-1.f, -2.f, -3.f}, {1.f, 2.f, 3.f}, current, &input.Drag.Current));
    ASSERT_EQ(BuildFrame(input, decorated, decorated_hits), FrameBuildResult::Built);
    EXPECT_EQ(decorated.LineCount, plain.LineCount + 25);
    EXPECT_EQ(decorated_hits.Count, plain_hits.Count);
    EXPECT_EQ(decorated.TriangleCount, plain.TriangleCount);
    EXPECT_EQ(decorated.SpriteCount, plain.SpriteCount);
    EXPECT_EQ(decorated.MeshLineGroupCount, 1u);
    EXPECT_TRUE(decorated.ValidMeshLineGroups());
    EXPECT_EQ(decorated.MeshLineGroups[0].First, 1u);
    EXPECT_EQ(decorated.MeshLineGroups[0].Count, 24u);
    EXPECT_EQ(decorated.Lines[0].Width, C::GuideWidthPx);
    EXPECT_EQ(decorated.Lines[0].DashOn, C::GuideDashOnPx);
    EXPECT_EQ(decorated.Lines[0].DashOff, C::GuideDashOffPx);
    EXPECT_EQ(decorated.Lines[0].Color, Packed(UI::Tokens::AccentRed, C::GuideAlpha));
    for (uint32_t i = 1; i <= 12; ++i)
    {
        EXPECT_EQ(decorated.Lines[i].DashOn, 0.f);
        EXPECT_EQ(decorated.Lines[i].Color, Packed(UI::Tokens::AccentOrange));
        EXPECT_EQ(decorated.Lines[i + 12].DashOn, C::GhostDashOnPx);
        EXPECT_EQ(decorated.Lines[i + 12].DashOff, C::GhostDashOffPx);
        EXPECT_EQ(decorated.Lines[i + 12].Color, Packed(UI::Tokens::AccentOrange, C::GhostAlpha));
    }
    for (const auto& shape : plain_hits.View())
    {
        EXPECT_EQ(decorated_hits.Pick(shape.Points[0]).Handle, plain_hits.Pick(shape.Points[0]).Handle);
    }
}

TEST(GizmoFrameBuilderTest, NonMeshDragUsesPixelSizedDashedStartMarkerAndContinuousDashPhase)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (uint32_t height : {600u, 1200u})
            {
                auto input            = Input(ortho, reverse, 10.f, height);
                input.Active          = Handle::TranslateXY;
                input.Drag.Visible    = true;
                input.Drag.StartPivot = {-1.f, .5f, 0.f};
                GizmoFrameData output = {};
                HitFrame       hits   = {};
                ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                EXPECT_EQ(output.MeshLineGroupCount, 0u);
                EXPECT_EQ(output.Lines[0].Color, Packed(UI::Tokens::AccentBlue, C::GuideAlpha));
                for (uint32_t i = 0; i < 4; ++i)
                {
                    const auto& edge = output.Lines[i + 1];
                    const auto  a = Screen(input, edge.A), b = Screen(input, edge.B);
                    EXPECT_NEAR(std::hypot(a.x - b.x, a.y - b.y), C::CenterSizePx, .01f);
                    EXPECT_EQ(edge.DashPhase, C::CenterSizePx * i * height / 600.f);
                    EXPECT_EQ(edge.Width, C::SelectionBoxWidthPx * height / 600.f);
                }
            }
        }
    }
}

TEST(GizmoFrameBuilderTest, InvalidOptionalBoxOmitsTheWholeBoxWithoutHidingOrUnpickingHandles)
{
    auto input         = Input();
    input.Active       = Handle::TranslateX;
    input.Drag.Visible = input.Drag.HasBox = true;
    input.Drag.MeshUUID                    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    input.Drag.Current[7].z                = std::numeric_limits<float>::quiet_NaN();
    GizmoFrameData output                  = {};
    HitFrame       hits                    = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
    EXPECT_EQ(output.MeshLineGroupCount, 0u);
    EXPECT_NE(hits.Pick(Screen(input, input.Pivot)).Handle, 0u);
    EXPECT_TRUE(Find(hits, Handle::TranslateX, HitPriority::AxisLine));
    EXPECT_EQ(output.Lines[0].DashOn, C::GuideDashOnPx);
}

TEST(GizmoFrameBuilderTest, RootDecorationsUseEachCapturedOriginAndDoNotDuplicateTheActiveTargetBox)
{
    auto input           = Input();
    input.Active         = Handle::TranslateX;
    input.Pivot          = {.5f, 0.f, 0.f};
    GizmoFrameData plain = {}, output = {};
    HitFrame       plain_hits = {}, hits = {};
    ASSERT_EQ(BuildFrame(input, plain, plain_hits), FrameBuildResult::Built);
    ZEngine::Core::Containers::FixedArray<TransformDecorations, 2> roots = {};
    roots[0]                                                             = {
        .StartPivot = {-1.f, 0.f, 0.f},
          .CurrentPivot = {-.5f, 0.f, 0.f},
          .MeshUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .HasBox = true, .Visible = true
    };
    ASSERT_TRUE(G::TransformBox({-.2f, -.3f, -.4f}, {.2f, .3f, .4f}, ComposeTransformMatrix(roots[0].StartPivot, {}, {1.f, 1.f, 1.f}), &roots[0].Ghost));
    ASSERT_TRUE(G::TransformBox({-.2f, -.3f, -.4f}, {.2f, .3f, .4f}, ComposeTransformMatrix(roots[0].CurrentPivot, {}, {1.f, 1.f, 1.f}), &roots[0].Current));
    roots[1] = {
        .StartPivot = { 2.f, 0.f, 0.f},
          .CurrentPivot = {2.5f, 0.f, 0.f},
          .Visible = true
    };
    input.Drag  = roots[0]; // ignored when the complete root presentation is supplied
    input.Roots = {roots.data(), 2};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_FALSE(output.DecorationsOmitted);
    EXPECT_EQ(output.LineCount, plain.LineCount + 30u);
    ASSERT_EQ(output.MeshLineGroupCount, 1u);
    EXPECT_EQ(output.MeshLineGroups[0].Count, 24u);
    EXPECT_EQ(output.Lines[0].A.x, -1.f);
    EXPECT_EQ(output.Lines[0].B.x, -.5f);
    EXPECT_EQ(output.Lines[25].A.x, 2.f);
    EXPECT_EQ(output.Lines[25].B.x, 2.5f);
    EXPECT_EQ(hits.Count, plain_hits.Count);
    EXPECT_EQ(output.SpriteCount, plain.SpriteCount);
}

TEST(GizmoFrameBuilderTest, ARejectedRootBoxDoesNotDropOtherRootsOrTheirSharedHandles)
{
    auto input                                                           = Input();
    input.Active                                                         = Handle::TranslateX;
    input.Drag.Visible                                                   = true;
    ZEngine::Core::Containers::FixedArray<TransformDecorations, 2> roots = {};
    for (auto& root : roots)
    {
        root = {.MeshUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .HasBox = true, .Visible = true};
    }
    roots[0].Current[7].z = std::numeric_limits<float>::quiet_NaN();
    input.Roots           = {roots.data(), 2};
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
    EXPECT_EQ(output.MeshLineGroupCount, 1u);
    EXPECT_TRUE(output.ValidMeshLineGroups());
    EXPECT_TRUE(Find(hits, Handle::TranslateX, HitPriority::AxisLine));
    EXPECT_EQ(hits.Pick(Screen(input, input.Pivot)).Handle, uint32_t(Handle::TranslateView));
}

TEST(GizmoFrameBuilderTest, RootDecorationPressureRetainsWedgesPivotMarkersAndAllVisibleHandles)
{
    ZEngine::Core::Containers::FixedArray<TransformDecorations, GizmoFrameData::MaxMeshLineGroups> roots = {};
    for (auto& root : roots)
    {
        root = {.MeshUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .HasBox = true, .Visible = true};
        ASSERT_TRUE(G::TransformBox({-.2f, -.3f, -.4f}, {.2f, .3f, .4f}, Identity<Mat4f>(), &root.Ghost));
        root.Current = root.Ghost;
    }
    GizmoFrameData plain = {}, output = {};
    HitFrame       plain_hits = {}, hits = {};
    for (auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        for (bool ortho : {false, true})
        {
            for (bool reverse : {false, true})
            {
                for (uint32_t height : {600u, 1200u})
                {
                    for (bool components : {false, true})
                    {
                        SCOPED_TRACE(testing::Message() << uint32_t(mode) << "," << ortho << "," << reverse << "," << height << "," << components);
                        auto input        = Input(ortho, reverse, 10.f, height);
                        input.Mode        = mode;
                        input.Active      = mode == Operation::Rotate ? Handle::RotateZ : mode == Operation::Scale ? Handle::ScaleUniform : Handle::TranslateX;
                        input.PivotMarker = {
                            .Origin = {-.5f, 0.f, 0.f},
                              .Visible = true
                        };
                        if (components)
                        {
                            input.Light = {
                                .Light = {.Type = ZEngine::ECS::Components::LightComponent::Type::Point, .Range = 1.f},
                                  .Valid = true
                            };
                            input.Frustum = {
                                .Camera = {.Near = .1f, .Far = 2.f},
                                  .Valid = true
                            };
                        }
                        if (mode == Operation::Rotate)
                        {
                            input.Rotation = {
                                .Axis = {0.f, 0.f, 1.f},
                                  .StartDirection = {1.f, 0.f, 0.f},
                                  .Angle = std::numbers::pi / 2.0, .Visible = true
                            };
                        }
                        if (mode == Operation::Scale)
                        {
                            float world_per_pixel = 0.f;
                            ASSERT_TRUE(G::WorldPerPixel(Projection(input), input.Pivot, input.Viewport.Size.y, &world_per_pixel));
                            input.Scale = {.Ratio = 1.5f, .HandleLength = world_per_pixel * C::AxisLengthPx, .Visible = true};
                        }
                        ASSERT_EQ(BuildFrame(input, plain, plain_hits), FrameBuildResult::Built);
                        input.Drag.Visible = true;
                        input.Roots        = {roots.data(), uint32_t(roots.size())};
                        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                        EXPECT_TRUE(output.DecorationsOmitted);
                        EXPECT_GT(output.MeshLineGroupCount, 0u);
                        EXPECT_LT(output.MeshLineGroupCount, roots.size());
                        EXPECT_TRUE(output.ValidCounts());
                        EXPECT_TRUE(output.ValidMeshLineGroups());
                        EXPECT_EQ(hits.Count, plain_hits.Count);
                        EXPECT_EQ(output.SpriteCount, plain.SpriteCount);
                        EXPECT_EQ(output.TriangleCount, plain.TriangleCount);
                        // The seven final pivot lines are higher priority than
                        // omitted roots and must match the unsaturated frame.
                        for (uint32_t i = 0; i < 7; ++i)
                        {
                            const auto& a = plain.Lines[plain.LineCount - 7 + i];
                            const auto& b = output.Lines[output.LineCount - 7 + i];
                            EXPECT_EQ(a.Width, b.Width);
                            EXPECT_EQ(a.Color, b.Color);
                            for (uint32_t axis = 0; axis < 3; ++axis)
                            {
                                EXPECT_FLOAT_EQ(a.A[axis], b.A[axis]);
                                EXPECT_FLOAT_EQ(a.B[axis], b.B[axis]);
                            }
                        }
                        for (uint32_t i = 0; i < plain_hits.Count; ++i)
                        {
                            EXPECT_EQ(hits.Shapes[i].Handle, plain_hits.Shapes[i].Handle);
                            EXPECT_EQ(hits.Shapes[i].Geometry, plain_hits.Shapes[i].Geometry);
                        }
                    }
                }
            }
        }
    }
}

TEST(GizmoFrameBuilderTest, MalformedRootViewOmitsOnlyDecorationsAndUnusedCapacityIsNeverRead)
{
    auto input            = Input();
    input.Active          = Handle::TranslateX;
    input.Drag.Visible    = true;
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    input.Roots           = {nullptr, 1};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
    EXPECT_TRUE(Find(hits, Handle::TranslateX, HitPriority::AxisLine));
    TransformDecorations one = {.Visible = true};
    input.Roots              = {&one, GizmoFrameData::MaxMeshLineGroups + 1};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
    ZEngine::Core::Containers::FixedArray<TransformDecorations, 2> roots = {};
    roots[0].Visible                                                     = true;
    roots[1].StartPivot.x                                                = std::numeric_limits<float>::quiet_NaN();
    input.Roots                                                          = {roots.data(), 1};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_FALSE(output.DecorationsOmitted);
    input.RootDecorationsOmitted = true;
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
}

TEST(GizmoFrameBuilderTest, ClippedRootBoxRangesRemainSortedAndNeverProduceExtraHitShapes)
{
    auto input                                                           = Input();
    input.Active                                                         = Handle::TranslateX;
    input.Drag.Visible                                                   = true;
    ZEngine::Core::Containers::FixedArray<TransformDecorations, 3> roots = {};
    for (uint32_t i = 0; i < roots.size(); ++i)
    {
        auto& root = roots[i];
        root       = {
            .StartPivot = {float(i), 0.f, 9.f},
              .CurrentPivot = {float(i), 0.f, 9.f},
              .MeshUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .HasBox = true, .Visible = true
        };
        ASSERT_TRUE(G::TransformBox({-.2f, -.3f, -2.f}, {.2f, .3f, 2.f}, ComposeTransformMatrix(root.StartPivot, {}, {1.f, 1.f, 1.f}), &root.Ghost));
        root.Current = root.Ghost;
    }
    input.Roots           = {roots.data(), uint32_t(roots.size())};
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_FALSE(output.DecorationsOmitted);
    EXPECT_EQ(output.MeshLineGroupCount, 3u);
    EXPECT_TRUE(output.ValidMeshLineGroups());
    for (uint32_t i = 0; i < output.MeshLineGroupCount; ++i)
    {
        EXPECT_LE(output.MeshLineGroups[i].Count, 24u);
    }
}

TEST(GizmoFrameBuilderTest, ScaleCubesAndShaftsShareLogicalSizesAcrossProjectionsDepthAndDpi)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (float distance : {1.f, 10.f, 100.f})
            {
                for (uint32_t height : {600u, 1200u})
                {
                    auto input = Input(ortho, reverse, distance, height);
                    input.Mode = Operation::Scale;
                    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                    EXPECT_EQ(output.SpriteCount, 3u); // X/Y and centre; view-parallel Z hidden
                    EXPECT_EQ(output.TriangleCount, 0u);
                    for (auto handle : {Handle::ScaleX, Handle::ScaleY})
                    {
                        const auto* cube = Find(hits, handle, HitPriority::ScaleCube);
                        ASSERT_NE(cube, nullptr);
                        EXPECT_FLOAT_EQ(cube->Padding, C::BoxHitPaddingPx);
                        EXPECT_NEAR(cube->Points[1].x - cube->Points[0].x, C::ScaleCubeSizePx, .001f);
                        const auto center = (cube->Points[0] + cube->Points[1]) * .5f;
                        EXPECT_EQ(hits.Pick(center).Handle, uint32_t(handle));
                        const auto pivot = Screen(input, input.Pivot);
                        EXPECT_NEAR(std::hypot(double(center.x - pivot.x), double(center.y - pivot.y)), C::AxisLengthPx, .02f);
                        ASSERT_NE(Find(hits, handle, HitPriority::AxisLine), nullptr);
                    }
                    EXPECT_EQ(hits.Pick(Screen(input, input.Pivot)).Handle, uint32_t(Handle::ScaleUniform));
                    for (uint32_t i = 0; i < output.SpriteCount; ++i)
                    {
                        EXPECT_EQ(output.Sprites[i].Size, C::ScaleCubeSizePx * height / 600.f);
                    }
                }
            }
        }
    }
}

TEST(GizmoFrameBuilderTest, ScaleHoverEmphasizesCubeAndShaftAndDimsOtherAxes)
{
    auto input            = Input();
    input.Mode            = Operation::Scale;
    input.Hovered         = Handle::ScaleY;
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    ASSERT_EQ(output.SpriteCount, 3u);
    EXPECT_EQ(output.Sprites[0].Fill, Packed(UI::Tokens::AccentRed, C::OtherHoveredAlpha));
    EXPECT_EQ(output.Sprites[0].Size, C::ScaleCubeSizePx);
    EXPECT_EQ(output.Sprites[1].Fill, Packed(UI::Tokens::AccentGreen));
    EXPECT_EQ(output.Sprites[1].Size, C::ScaleEmphasizedCubeSizePx);
    EXPECT_EQ(output.Sprites[2].Fill, Packed(UI::Tokens::AccentOrange));
    ASSERT_EQ(output.LineCount, 3u);
    EXPECT_EQ(output.Lines[1].Width, C::GlowWidthPx);
    EXPECT_EQ(output.Lines[2].Width, C::AxisEmphasizedWidthPx);
    const auto* cube = Find(hits, Handle::ScaleY, HitPriority::ScaleCube);
    ASSERT_NE(cube, nullptr);
    EXPECT_NEAR(cube->Points[1].x - cube->Points[0].x, C::ScaleEmphasizedCubeSizePx, .001f);
}

TEST(GizmoFrameBuilderTest, NearZeroScaleAxesStayAtFortyPercentWithoutCubeOrShaftHits)
{
    auto input            = Input();
    input.Mode            = Operation::Scale;
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (float scale : {0.f, -0.f, 1e-5f, -1e-5f})
    {
        input.AuthoredScale.x = scale;
        input.Hovered         = Handle::ScaleY;
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        ASSERT_EQ(output.SpriteCount, 3u);
        EXPECT_EQ(output.Sprites[0].Fill, Packed(UI::Tokens::AccentRed, C::ScaleDisabledAlpha));
        EXPECT_EQ(output.Sprites[0].Size, C::ScaleCubeSizePx);
        EXPECT_EQ(Find(hits, Handle::ScaleX, HitPriority::ScaleCube), nullptr);
        EXPECT_EQ(Find(hits, Handle::ScaleX, HitPriority::AxisLine), nullptr);
        EXPECT_EQ(hits.Pick(Screen(input, output.Sprites[0].Position)).Handle, 0u);
    }
    input.AuthoredScale.x = -C::MinimumEditableScale;
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_NE(Find(hits, Handle::ScaleX, HitPriority::ScaleCube), nullptr);
}

TEST(GizmoFrameBuilderTest, ScaleDragRetainsStartLengthAndAddsOnlyUnpickableGhosts)
{
    for (const auto active : {Handle::ScaleX, Handle::ScaleUniform})
    {
        auto input   = Input();
        input.Mode   = Operation::Scale;
        input.Active = active;
        float wpp    = 0.f;
        ASSERT_TRUE(G::WorldPerPixel(Projection(input), input.Pivot, input.Viewport.Size.y, &wpp));
        input.Scale           = {.Ratio = 1.5f, .HandleLength = C::AxisLengthPx * wpp, .Visible = true};
        GizmoFrameData output = {};
        HitFrame       hits   = {};
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        for (auto handle : {Handle::ScaleX, Handle::ScaleY})
        {
            const auto* cube = Find(hits, handle, HitPriority::ScaleCube);
            ASSERT_NE(cube, nullptr);
            const auto center = (cube->Points[0] + cube->Points[1]) * .5f;
            const auto pivot  = Screen(input, input.Pivot);
            EXPECT_NEAR(std::hypot(double(center.x - pivot.x), double(center.y - pivot.y)), C::AxisLengthPx * (active == Handle::ScaleUniform || active == handle ? 1.5f : 1.f), .01f);
        }
        EXPECT_EQ(output.LineCount, active == Handle::ScaleUniform ? 10u : 7u);
        const uint32_t ghost_lines = active == Handle::ScaleUniform ? 8u : 4u;
        for (uint32_t i = 0; i < ghost_lines; ++i)
        {
            EXPECT_EQ(output.Lines[i].DashOn, C::ScaleGhostDashOnPx);
            EXPECT_EQ(output.Lines[i].DashOff, C::ScaleGhostDashOffPx);
            EXPECT_EQ(output.Lines[i].DashPhase, C::ScaleCubeSizePx * (i % 4));
        }
        EXPECT_EQ(output.Sprites[output.SpriteCount - 1].Size, active == Handle::ScaleUniform ? C::ScaleUniformCubeSizePx : C::ScaleCubeSizePx);
        EXPECT_EQ(output.Sprites[output.SpriteCount - 1].Outline, active == Handle::ScaleUniform ? Packed(UI::Tokens::TextPrimary) : 0u);
        EXPECT_EQ(hits.Count, 5u);
    }
}

TEST(GizmoFrameBuilderTest, ScaleMinimumRatioNeverReversesShaftIntoCentre)
{
    auto input   = Input();
    input.Mode   = Operation::Scale;
    input.Active = Handle::ScaleX;
    float wpp    = 0.f;
    ASSERT_TRUE(G::WorldPerPixel(Projection(input), input.Pivot, input.Viewport.Size.y, &wpp));
    input.Scale           = {.Ratio = C::MinimumScaleRatio, .HandleLength = C::AxisLengthPx * wpp, .Visible = true};
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(Find(hits, Handle::ScaleX, HitPriority::AxisLine), nullptr);
    EXPECT_NE(Find(hits, Handle::ScaleX, HitPriority::ScaleCube), nullptr);
    EXPECT_EQ(hits.Pick(Screen(input, input.Pivot)).Handle, uint32_t(Handle::ScaleUniform));
}

TEST(GizmoFrameBuilderTest, InvalidScalePresentationFailsClosedWithoutClearingUnusedStorage)
{
    auto input                                          = Input();
    input.Mode                                          = Operation::Scale;
    input.Active                                        = Handle::ScaleX;
    input.Scale                                         = {.Ratio = 1.f, .HandleLength = 1.f, .Visible = true};
    GizmoFrameData output                               = {};
    HitFrame       hits                                 = {};
    output.Sprites[GizmoFrameData::MaxSprites - 1].Size = 123.f;
    hits.Shapes[HitFrame::MaxShapes - 1].Handle         = 999u;
    for (float invalid : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        input.Scale.Ratio = invalid;
        EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Invalid);
        ExpectCleared(output, hits);
    }
    input.Scale.Visible   = false;
    input.AuthoredScale.y = std::numeric_limits<float>::infinity();
    EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Invalid);
    EXPECT_EQ(output.Sprites[GizmoFrameData::MaxSprites - 1].Size, 123.f);
    EXPECT_EQ(hits.Shapes[HitFrame::MaxShapes - 1].Handle, 999u);
}

TEST(GizmoFrameBuilderTest, RecordsDefaultToNoEditableGeometry)
{
    FrameInput     input  = {};
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    EXPECT_EQ(input.Axes[0].x, 1.f);
    EXPECT_EQ(input.Axes[1].y, 1.f);
    EXPECT_EQ(input.Axes[2].z, 1.f);
    EXPECT_EQ(input.Mode, Operation::Select);
    EXPECT_EQ(input.Active, Handle::None);
    EXPECT_FALSE(input.Editable);
    EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
    ExpectCleared(output, hits);
}

TEST(GizmoFrameBuilderTest, FrontCameraOmitsParallelZAndEdgeOnPlanes)
{
    const auto     input  = Input();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, 6u);     // XY border and X/Y shafts
    EXPECT_EQ(output.TriangleCount, 4u); // XY fill and X/Y heads
    EXPECT_EQ(output.SpriteCount, 1u);
    EXPECT_EQ(hits.Count, 7u);
    EXPECT_EQ(output.Triangles[0].AntiAliasEdges, 5u);
    EXPECT_EQ(output.Triangles[1].AntiAliasEdges, 3u);
    EXPECT_FALSE(Find(hits, Handle::TranslateZ, HitPriority::AxisLine));
    EXPECT_FALSE(Find(hits, Handle::TranslateYZ, HitPriority::Plane));
    EXPECT_FALSE(Find(hits, Handle::TranslateZX, HitPriority::Plane));
    EXPECT_EQ(output.Sprites[0].Fill, Packed(UI::Tokens::AccentOrange));
}

TEST(GizmoFrameBuilderTest, LogicalSizesSurviveProjectionDistanceReverseDepthAndDpi)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (float distance : {1.f, 10.f, 100.f})
            {
                for (uint32_t height : {600u, 1200u})
                {
                    const auto input = Input(ortho, reverse, distance, height);
                    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                    const auto* shaft = Find(hits, Handle::TranslateX, HitPriority::AxisLine);
                    const auto* head  = Find(hits, Handle::TranslateX, HitPriority::Arrowhead);
                    ASSERT_NE(shaft, nullptr);
                    ASSERT_NE(head, nullptr);
                    const auto center = Screen(input, input.Pivot);
                    EXPECT_NEAR(shaft->Points[0].x - center.x, C::AxisStartPx, 0.01f);
                    EXPECT_NEAR(shaft->Points[1].x - center.x, C::AxisLengthPx - C::ArrowLengthPx, 0.01f);
                    EXPECT_NEAR(head->Points[0].x - center.x, C::AxisLengthPx, 0.01f);
                    EXPECT_NEAR(head->Points[0].x - head->Points[1].x, C::ArrowLengthPx, 0.01f);
                    const auto a = Screen(input, output.Triangles[2].B), b = Screen(input, output.Triangles[2].C);
                    EXPECT_NEAR(std::hypot(double(a.x) - b.x, double(a.y) - b.y), C::ArrowWidthPx, 0.01f);
                    EXPECT_EQ(output.Lines[4].Width, C::AxisIdleWidthPx * height / 600.f);
                    EXPECT_EQ(output.Sprites[0].Size, C::CenterSizePx * height / 600.f);
                    EXPECT_EQ(hits.Pick(center).Handle, uint32_t(Handle::TranslateView));
                }
            }
        }
    }
}

TEST(GizmoFrameBuilderTest, PlaneUsesPositiveAxesAndNormalColorWithExactMetrics)
{
    const auto     input  = Input();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    const auto  center = Screen(input, input.Pivot);
    const auto* plane  = Find(hits, Handle::TranslateXY, HitPriority::Plane);
    ASSERT_NE(plane, nullptr);
    EXPECT_EQ(plane->Geometry, HitGeometry::Triangle);
    EXPECT_EQ(plane->Padding, C::PlaneHitPaddingPx);
    EXPECT_NEAR(plane->Points[0].x - center.x, C::PlaneStartPx, 0.001f);
    EXPECT_NEAR(center.y - plane->Points[0].y, C::PlaneStartPx, 0.001f);
    EXPECT_NEAR(plane->Points[1].x - center.x, C::PlaneEndPx, 0.001f);
    EXPECT_NEAR(center.y - plane->Points[2].y, C::PlaneEndPx, 0.001f);
    EXPECT_EQ(output.Triangles[0].Color, Packed(UI::Tokens::AccentBlue, C::PlaneIdleAlpha));
    EXPECT_EQ(output.Lines[0].Color, Packed(UI::Tokens::AccentBlue, C::PlaneIdleBorderAlpha));
    EXPECT_EQ(hits.Pick(center + Vec2f(50.f, -50.f)).Handle, uint32_t(Handle::TranslateXY));
    EXPECT_EQ(hits.Pick(center + Vec2f(30.f, -50.f)).Handle, 0u);
}

TEST(GizmoFrameBuilderTest, HoverAndActiveHaveApprovedPaintOrderAlphasAndNoninteractiveGlow)
{
    auto           input  = Input();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    input.Hovered         = Handle::TranslateX;
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, 7u);
    EXPECT_EQ(hits.Count, 7u); // glow and border lines cannot pick
    EXPECT_EQ(output.Lines[4].Color, Packed(UI::Tokens::AccentGreen, C::OtherHoveredAlpha));
    EXPECT_EQ(output.Lines[5].Width, C::GlowWidthPx);
    EXPECT_EQ(output.Lines[5].Color, Packed(UI::Tokens::AccentRed, C::GlowAlpha));
    EXPECT_EQ(output.Lines[6].Width, C::AxisEmphasizedWidthPx);
    EXPECT_EQ(output.Lines[6].Color, Packed(UI::Tokens::AccentRed));
    EXPECT_EQ(output.Sprites[0].Fill, Packed(UI::Tokens::AccentOrange));
    input.Active = Handle::TranslateY; // active beats a stale hover
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.Lines[4].Color, Packed(UI::Tokens::AccentRed, C::OtherActiveAlpha));
    EXPECT_EQ(output.Lines[6].Color, Packed(UI::Tokens::AccentGreen));
}

TEST(GizmoFrameBuilderTest, HoveredPlaneHasFiftyFivePercentFillAndTwoPixelBorder)
{
    auto input            = Input();
    input.Hovered         = Handle::TranslateXY;
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.Triangles[0].Color, Packed(UI::Tokens::AccentBlue, C::PlaneEmphasizedAlpha));
    EXPECT_EQ(output.Lines[0].Width, C::PlaneEmphasizedBorderWidthPx);
    EXPECT_EQ(output.Lines[0].Color, Packed(UI::Tokens::AccentBlue));
    EXPECT_EQ(output.Lines[4].Color, Packed(UI::Tokens::AccentRed, C::OtherHoveredAlpha));
}

TEST(GizmoFrameBuilderTest, ResolvedLocalAxesCanBeRotatedAndScaledWithoutActorPointers)
{
    auto input = Input();
    input.Axes = {
        {{0.f, 4.f, 0.f}, {-2.f, 0.f, 0.f}, {0.f, 0.f, 0.5f}}
    };
    input.Pivot           = {1.f, -1.f, -1.f};
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    const auto* x = Find(hits, Handle::TranslateX, HitPriority::Arrowhead);
    const auto* y = Find(hits, Handle::TranslateY, HitPriority::Arrowhead);
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);
    const auto center = Screen(input, input.Pivot);
    EXPECT_NEAR(center.y - x->Points[0].y, C::AxisLengthPx, 0.001f);
    EXPECT_NEAR(center.x - y->Points[0].x, C::AxisLengthPx, 0.001f);
}

TEST(GizmoFrameBuilderTest, AxisAndPlaneFadesUseTheirApprovedLinearIntervals)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (float cosine : {0.94f, 0.97f, 0.995f})
    {
        auto        input = Input(true);
        const float sine  = std::sqrt(1.f - cosine * cosine);
        input.Axes        = {
            {{sine, 0.f, cosine}, {0.f, 1.f, 0.f}, {-cosine, 0.f, sine}}
        };
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        const auto* shape = Find(hits, Handle::TranslateX, HitPriority::AxisLine);
        if (cosine > C::AxisHide)
        {
            EXPECT_EQ(shape, nullptr);
        }
        else
        {
            ASSERT_NE(shape, nullptr);
            const float alpha = std::clamp((C::AxisHide - cosine) / (C::AxisHide - C::AxisFadeStart), 0.f, 1.f);
            bool        found = false;
            for (uint32_t i = 0; i < output.LineCount; ++i)
            {
                found |= output.Lines[i].Color == Packed(UI::Tokens::AccentRed, alpha);
            }
            EXPECT_TRUE(found);
        }
    }
    for (float cosine : {0.05f, 0.175f, 0.3f})
    {
        auto        input = Input(true);
        const float sine  = std::sqrt(1.f - cosine * cosine);
        input.Axes        = {
            {{cosine, 0.f, -sine}, {0.f, 1.f, 0.f}, {sine, 0.f, cosine}}
        };
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        const auto* shape = Find(hits, Handle::TranslateXY, HitPriority::Plane);
        if (cosine < C::PlaneHide)
        {
            EXPECT_EQ(shape, nullptr);
        }
        else
        {
            ASSERT_NE(shape, nullptr);
            const float alpha = C::PlaneIdleAlpha * std::clamp((cosine - C::PlaneHide) / (C::PlaneFadeEnd - C::PlaneHide), 0.f, 1.f);
            bool        found = false;
            for (uint32_t i = 0; i < output.TriangleCount; ++i)
            {
                found |= output.Triangles[i].Color == Packed(UI::Tokens::AccentBlue, alpha);
            }
            EXPECT_TRUE(found);
        }
    }
}

TEST(GizmoFrameBuilderTest, NearGuardUsesActualStandardAndReverseProjectionForBothKinds)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (float depth : {-1.f, 0.f, 0.05f, 0.15f, 0.3f})
            {
                auto input    = Input(ortho, reverse);
                input.Pivot.z = 10.f - depth;
                EXPECT_EQ(BuildFrame(input, output, hits), depth < 0.2f ? FrameBuildResult::Hidden : FrameBuildResult::Built);
                if (depth < 0.2f)
                {
                    ExpectCleared(output, hits);
                }
            }
        }
    }
}

TEST(GizmoFrameBuilderTest, NearCrossingHandlesPublishAndPickOnlyClippedGeometry)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool reverse : {false, true})
    {
        auto input          = Input(false, reverse, 0.3f);
        input.Viewport.Size = {80.f, 60.f};
        input.TargetWidth   = 800;
        input.TargetHeight  = 600;
        const float d       = std::sqrt(0.5f);
        input.Axes          = {
            {{d, 0.f, d}, {0.f, 1.f, 0.f}, {-d, 0.f, d}}
        };
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        const auto* x = Find(hits, Handle::TranslateX, HitPriority::AxisLine);
        ASSERT_NE(x, nullptr);
        EXPECT_GT(x->Points[1].x, input.Viewport.Origin.x + input.Viewport.Size.x); // clipped, never clamped
        const auto frame = Projection(input);
        for (uint32_t i = 0; i < output.LineCount; ++i)
        {
            for (const auto& point : {output.Lines[i].A, output.Lines[i].B})
            {
                G::ProjectedPoint projected = {};
                ASSERT_TRUE(G::Project(point, frame, input.Viewport, &projected));
                EXPECT_GE(projected.Depth, -0.0001f);
                EXPECT_LE(projected.Depth, 1.0001f);
            }
        }
        EXPECT_EQ(hits.Pick(x->Points[1]).Handle, 0u);                        // off-viewport padding cannot capture
        EXPECT_FALSE(Find(hits, Handle::TranslateX, HitPriority::Arrowhead)); // tip is behind the eye
    }
}

TEST(GizmoFrameBuilderTest, NearGuardTracksCapturedProjectionRatherThanADefaultNearValue)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (float near_distance : {0.01f, 0.1f, 1.f})
            {
                for (float factor : {1.5f, 2.5f})
                {
                    const auto input = Input(ortho, reverse, near_distance * factor, 600, near_distance);
                    EXPECT_EQ(BuildFrame(input, output, hits), factor < 2.f ? FrameBuildResult::Hidden : FrameBuildResult::Built);
                    if (factor < 2.f)
                    {
                        ExpectCleared(output, hits);
                    }
                }
            }
        }
    }
}

TEST(GizmoFrameBuilderTest, FullyFarClippedFrameCannotLeaveInvisibleHitShapes)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            auto input = Input(ortho, reverse);
            ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
            input.Pivot.z = -2000.f;
            EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
            ExpectCleared(output, hits);
        }
    }
}

TEST(GizmoFrameBuilderTest, PoseSweepStaysWithinFixedBudgetsAcrossNearAndFarClipping)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (float distance : {0.3f, 1.f, 10.f, 999.f})
            {
                for (uint32_t step = 0; step < 36; ++step)
                {
                    auto input          = Input(ortho, reverse, distance);
                    input.Viewport.Size = {80.f, 60.f};
                    input.Active        = Handle::TranslateX;
                    const auto rotation = ComposeTransformMatrix({}, {float(step) * 0.1f, float(step) * 0.3f, float(step) * 0.07f}, {1.f, 1.f, 1.f});
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        input.Axes[axis] = {rotation(0, axis), rotation(1, axis), rotation(2, axis)};
                    }
                    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built) << ortho << reverse << distance << step;
                    ASSERT_LE(hits.Count, HitFrame::MaxShapes);
                    ASSERT_TRUE(output.ValidCounts());
                    for (const auto& shape : hits.View())
                    {
                        EXPECT_GT(shape.Depth, 0.f);
                        for (const auto& point : shape.Points)
                        {
                            EXPECT_TRUE(std::isfinite(point.x) && std::isfinite(point.y));
                        }
                    }
                }
            }
        }
    }
}

TEST(GizmoFrameBuilderTest, EveryHitPrimitiveMatchesPublishedRenderGeometry)
{
    auto        input = Input();
    const float d     = std::sqrt(0.5f);
    input.Axes        = {
        {{d, 0.f, d}, {0.5f, d, -0.5f}, {-0.5f, d, 0.5f}}
    };
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    for (const auto& shape : hits.View())
    {
        bool found = false;
        if (shape.Priority == HitPriority::Plane || shape.Priority == HitPriority::Arrowhead)
        {
            for (uint32_t i = 0; i < output.TriangleCount; ++i)
            {
                const auto& triangle = output.Triangles[i];
                const auto  a = Screen(input, triangle.A), b = Screen(input, triangle.B), c = Screen(input, triangle.C);
                if (shape.Priority == HitPriority::Plane)
                {
                    found |= shape.Points[0] == a && shape.Points[1] == b && shape.Points[2] == c;
                }
                else
                {
                    found |= shape.Points[0] == a && shape.Points[1] == (b + c) * 0.5f;
                }
            }
        }
        else if (shape.Priority == HitPriority::AxisLine)
        {
            for (uint32_t i = 0; i < output.LineCount; ++i)
            {
                found |= shape.Points[0] == Screen(input, output.Lines[i].A) && shape.Points[1] == Screen(input, output.Lines[i].B);
            }
        }
        else if (shape.Priority == HitPriority::Center)
        {
            found = output.SpriteCount == 1;
        }
        EXPECT_TRUE(found);
        EXPECT_GT(shape.Depth, 0.f);
    }
}

TEST(GizmoFrameBuilderTest, HiddenModesAndNoneditableTargetsClearPreviousFrame)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (auto mode : {Operation::Select, Operation(255)})
    {
        auto input = Input();
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        input.Mode = mode;
        EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
        ExpectCleared(output, hits);
    }
    auto input     = Input();
    input.Editable = false;
    EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
    ExpectCleared(output, hits);
}

TEST(GizmoFrameBuilderTest, MalformedCameraViewportAndAxesFailWithoutPartialFrame)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (uint32_t invalid = 0; invalid < 12; ++invalid)
    {
        auto input = Input();
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        switch (invalid)
        {
            case 0:
                input.Camera.Projection = {};
                break;
            case 1:
                input.Camera.ProjectionType = CameraType::UNDEFINED;
                break;
            case 2:
                input.TargetWidth = 0;
                break;
            case 3:
                input.TargetHeight = 0;
                break;
            case 4:
                input.Viewport.Size.y = 0.f;
                break;
            case 5:
                input.Viewport.Origin.x = std::numeric_limits<float>::infinity();
                break;
            case 6:
                input.Pivot.x = std::numeric_limits<float>::quiet_NaN();
                break;
            case 7:
                input.Axes[0] = {};
                break;
            case 8:
                input.Axes[0] = input.Axes[1];
                break;
            case 9:
                input.Axes[0] = {1e-7f, 1.f, 0.f};
                break;
            case 10:
                input.Active = Handle(255);
                break;
            case 11:
                input.Axes[1].y = std::numeric_limits<float>::infinity();
                break;
        }
        EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Invalid) << invalid;
        ExpectCleared(output, hits);
    }
}

TEST(GizmoFrameBuilderTest, ResetAndRepeatedBuildReuseStorageWithoutZeroingUnusedCapacity)
{
    auto           input                             = Input();
    GizmoFrameData output                            = {};
    HitFrame       hits                              = {};
    output.Lines[GizmoFrameData::MaxLines - 1].Color = 0x12345678u;
    hits.Shapes[HitFrame::MaxShapes - 1].Handle      = 12345;
    const auto* line_storage                         = output.Lines;
    const auto* hit_storage                          = hits.Shapes.data();
    for (uint32_t i = 0; i < 1000; ++i)
    {
        input.Hovered = i % 2 ? Handle::TranslateX : Handle::None;
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        ASSERT_LE(hits.Count, HitFrame::MaxShapes);
        ASSERT_TRUE(output.ValidCounts());
    }
    EXPECT_EQ(output.Lines, line_storage);
    EXPECT_EQ(hits.Shapes.data(), hit_storage);
    EXPECT_EQ(output.Lines[GizmoFrameData::MaxLines - 1].Color, 0x12345678u);
    EXPECT_EQ(hits.Shapes[HitFrame::MaxShapes - 1].Handle, 12345u);
}

TEST(GizmoFrameBuilderTest, CorruptHitCountAndNonfinitePointerCannotCapture)
{
    const auto     input  = Input();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(hits.Pick({std::numeric_limits<float>::quiet_NaN(), 361.f}).Handle, 0u);
    const auto viewport  = hits.Viewport;
    hits.Viewport.Size.x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(hits.Pick({437.f, 361.f}).Handle, 0u);
    hits.Viewport = viewport;
    hits.Count    = HitFrame::MaxShapes + 1;
    EXPECT_EQ(hits.Pick({437.f, 361.f}).Handle, 0u);
}

TEST(GizmoRotationFrameTest, IdleDrawsFourCompleteRingsAndOnlyAxisFrontHalvesAreHittable)
{
    auto input            = Input();
    input.Mode            = Operation::Rotate;
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(output.LineCount, 5u * C::RingSegments);
    EXPECT_EQ(output.TriangleCount, 0u);
    EXPECT_EQ(output.SpriteCount, 1u);
    uint32_t counts[4] = {};
    for (uint32_t i = 0; i < hits.Count; ++i)
    {
        const auto& shape = hits.Shapes[i];
        ASSERT_GE(shape.Handle, uint32_t(Handle::RotateX));
        ASSERT_LE(shape.Handle, uint32_t(Handle::RotateView));
        ++counts[shape.Handle - uint32_t(Handle::RotateX)];
        EXPECT_EQ(shape.Padding, C::LineHitTolerancePx);
        for (const auto& point : {hits.RingSpans[i].A, hits.RingSpans[i].B})
        {
            if (shape.Handle != uint32_t(Handle::RotateView))
            {
                EXPECT_GE(point.z, -.000001f);
            }
        }
        EXPECT_EQ(shape.Priority, shape.Handle == uint32_t(Handle::RotateView) ? HitPriority::ViewRing : HitPriority::AxisRing);
    }
    EXPECT_GT(counts[0], 0u);
    EXPECT_GT(counts[1], 0u);
    // Under the strict GZ5 half-space, a perfectly face-on axis ring has no
    // strictly nearer half. Its camera-facing view ring remains fully hittable.
    EXPECT_EQ(counts[2], 0u);
    EXPECT_EQ(counts[3], C::RingSegments);
    EXPECT_EQ(hits.Pick({547.f, 361.f}).Handle, uint32_t(Handle::RotateView));
    EXPECT_EQ(hits.Pick({556.f, 361.f}).Handle, 0u);
}

TEST(GizmoRotationFrameTest, ViewRingHasConstantLogicalRadiusAcrossDepthProjectionAndDpi)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (float distance : {3.f, 10.f, 40.f})
            {
                for (uint32_t height : {600u, 1200u})
                {
                    auto input            = Input(ortho, reverse, distance, height);
                    input.Mode            = Operation::Rotate;
                    GizmoFrameData output = {};
                    HitFrame       hits   = {};
                    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                    const auto center = Screen(input, input.Pivot);
                    for (uint32_t i = 3 * C::RingSegments; i < 4 * C::RingSegments; ++i)
                    {
                        const auto point = Screen(input, output.Lines[i].A);
                        EXPECT_NEAR(std::hypot(point.x - center.x, point.y - center.y), C::ViewRingRadiusPx, .001f);
                        EXPECT_FLOAT_EQ(output.Lines[i].Width, C::ViewRingContrastOutlineWidthPx * height / 600.f);
                        EXPECT_EQ(output.Lines[i].Color, Packed(UI::Tokens::BgViewport));
                    }
                    for (uint32_t i = 4 * C::RingSegments; i < output.LineCount; ++i)
                    {
                        const auto point = Screen(input, output.Lines[i].A);
                        EXPECT_NEAR(std::hypot(point.x - center.x, point.y - center.y), C::ViewRingRadiusPx, .001f);
                        EXPECT_FLOAT_EQ(output.Lines[i].Width, C::RingIdleWidthPx * height / 600.f);
                        EXPECT_EQ(output.Lines[i].Color, Packed(UI::Tokens::TextPrimary, C::ViewRingIdleAlpha));
                    }
                }
            }
        }
    }
}

TEST(GizmoRotationFrameTest, OffCenterOrthographicFaceOnAxisCannotStealTheViewRingOrBecomeHittable)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (uint32_t height : {600u, 1200u})
            {
                for (const auto pivot : {
                     Vec3f{},
                     Vec3f{1.5f, .75f, 0.f},
                     Vec3f{-1.5f, .75f, 0.f},
                     Vec3f{.75f, -1.5f, 0.f}
                })
                {
                    SCOPED_TRACE(::testing::Message() << ortho << reverse << height << pivot.x << ',' << pivot.y);
                    auto input            = Input(ortho, reverse, 10.f, height);
                    input.Mode            = Operation::Rotate;
                    input.Pivot           = pivot;
                    GizmoFrameData output = {};
                    HitFrame       hits   = {};
                    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                    ASSERT_EQ(output.LineCount, 5 * C::RingSegments);
                    uint32_t axis_count = 0, view_count = 0;
                    for (const auto& shape : hits.View())
                    {
                        axis_count += shape.Handle == uint32_t(Handle::RotateZ);
                        view_count += shape.Handle == uint32_t(Handle::RotateView);
                    }
                    EXPECT_EQ(view_count, C::RingSegments);
                    if (ortho || (pivot.x == 0.f && pivot.y == 0.f))
                    {
                        EXPECT_EQ(axis_count, 0u);
                    }
                    else
                    {
                        EXPECT_GT(axis_count, 0u); // Perspective retains its pivot-to-eye half-space.
                    }
                    if (!ortho)
                    {
                        continue;
                    }
                    for (uint32_t segment : {8u, 24u, 40u, 56u})
                    {
                        // Away from X/Y crossings and outside the view ring's
                        // tolerance: the face-on Z artwork has no pickable half.
                        const auto& line  = output.Lines[2 * C::RingSegments + segment];
                        const auto  point = Screen(input, (line.A + line.B) * .5f);
                        EXPECT_EQ(hits.Pick(point).Handle, 0u);
                    }
                }
            }
        }
    }
}

TEST(GizmoRotationFrameTest, TiltedOrthographicRingHitSpansFollowParallelViewRaysNotTheEyePosition)
{
    for (bool reverse : {false, true})
    {
        for (uint32_t height : {600u, 1200u})
        {
            for (bool tilted_camera : {false, true})
            {
                for (const auto pivot : {
                     Vec3f{ 1.5f,  .75f, 0.f},
                     Vec3f{-1.5f, -.75f, 0.f}
                })
                {
                    SCOPED_TRACE(::testing::Message() << reverse << height << tilted_camera << pivot.x << ',' << pivot.y);
                    auto input = Input(true, reverse, 10.f, height);
                    if (tilted_camera)
                    {
                        FlyCamera camera(4.f / 3.f, {});
                        camera.SetPosition({3.f, 2.f, 10.f});
                        camera.SetOrientation(10.f, -15.f);
                        camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
                        input.Camera              = camera.CaptureFrameData();
                        input.Camera.UsesReverseZ = reverse;
                        if (reverse)
                        {
                            for (uint32_t i = 0; i < 4; ++i)
                            {
                                input.Camera.Projection(2, i) = input.Camera.Projection(3, i) - input.Camera.Projection(2, i);
                            }
                        }
                    }
                    const auto rotation = ComposeTransformMatrix({}, {.37f, -.51f, .63f}, {1.f, 1.f, 1.f});
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        input.Axes[axis] = {rotation(0, axis), rotation(1, axis), rotation(2, axis)};
                    }
                    input.Mode            = Operation::Rotate;
                    input.Pivot           = pivot;
                    GizmoFrameData output = {};
                    HitFrame       hits   = {};
                    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                    ASSERT_EQ(output.LineCount, 5 * C::RingSegments); // No artwork is culled by this picking rule.
                    const auto projection = Projection(input);
                    uint32_t   counts[4]  = {};
                    for (uint32_t i = 0; i < hits.Count; ++i)
                    {
                        const auto& shape = hits.Shapes[i];
                        const auto  index = shape.Handle - uint32_t(Handle::RotateX);
                        ASSERT_LT(index, 4u);
                        ++counts[index];
                        if (index == 3)
                        {
                            continue;
                        }
                        // Independently evaluate signed view-space depth. All
                        // orthographic rays share this direction, even off-axis.
                        for (const auto& endpoint : {hits.RingSpans[i].A, hits.RingSpans[i].B})
                        {
                            double front = 0.0;
                            for (uint32_t axis = 0; axis < 3; ++axis)
                            {
                                front -= (double(endpoint[axis]) - pivot[axis]) * projection.Forward[axis];
                            }
                            EXPECT_GE(front, -1e-6);
                        }
                        const auto point = (shape.Points[0] + shape.Points[1]) * .5f;
                        Vec3f      world = {};
                        ASSERT_TRUE(hits.PickRingPoint(Handle(shape.Handle), point, projection, world));
                        const auto screen = Screen(input, world);
                        EXPECT_NEAR(screen.x, point.x, .001f);
                        EXPECT_NEAR(screen.y, point.y, .001f);
                    }
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        EXPECT_GE(counts[axis], C::RingSegments / 2);
                        EXPECT_LE(counts[axis], C::RingSegments / 2 + 2);
                    }
                    EXPECT_EQ(counts[3], C::RingSegments);
                }
            }
        }
    }
}

TEST(GizmoRotationFrameTest, EmphasizedRingPaintsLastWithGlowAndCorrectOtherAlphas)
{
    for (bool active : {false, true})
    {
        for (const auto handle : {Handle::RotateZ, Handle::RotateView})
        {
            auto input                              = Input();
            input.Mode                              = Operation::Rotate;
            (active ? input.Active : input.Hovered) = handle;
            GizmoFrameData output                   = {};
            HitFrame       hits                     = {};
            ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
            const bool view = handle == Handle::RotateView;
            EXPECT_EQ(output.LineCount, (view ? 4u : 6u) * C::RingSegments);
            for (uint32_t i = output.LineCount - C::RingSegments; i < output.LineCount; ++i)
            {
                EXPECT_EQ(output.Lines[i].Color, Packed(ConstraintColor(handle)));
                EXPECT_EQ(output.Lines[i].Width, C::RingEmphasizedWidthPx);
            }
            if (!view)
            {
                EXPECT_EQ(output.Lines[output.LineCount - 2 * C::RingSegments].Color, Packed(ConstraintColor(handle), C::GlowAlpha));
            }
            EXPECT_EQ(output.Lines[0].Color, Packed(UI::Tokens::AccentRed, active ? C::OtherActiveAlpha : C::OtherHoveredAlpha));
        }
    }
}

TEST(GizmoRotationFrameTest, EveryProjectedRingSharesExactUnitJoinOwnershipWithoutChangingHitGeometryOrGrowingStorage)
{
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (uint32_t height : {600u, 1200u})
            {
                auto      input = Input(ortho, reverse, 10.f, height);
                FlyCamera camera(4.f / 3.f, {});
                camera.SetPosition({7.f, 5.f, 10.f});
                camera.SetOrientation(25.f, -35.f);
                if (ortho)
                {
                    camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
                }
                input.Camera              = camera.CaptureFrameData();
                input.Camera.UsesReverseZ = reverse;
                if (reverse)
                {
                    for (uint32_t i = 0; i < 4; ++i)
                    {
                        input.Camera.Projection(2, i) = input.Camera.Projection(3, i) - input.Camera.Projection(2, i);
                    }
                }
                input.Mode            = Operation::Rotate;
                input.Hovered         = Handle::RotateZ;
                GizmoFrameData output = {};
                HitFrame       hits   = {};
                ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                ASSERT_EQ(output.LineCount, 6 * C::RingSegments);
                for (uint32_t block = 0; block < 6; ++block)
                {
                    for (uint32_t vertex = 0; vertex < C::RingSegments; ++vertex)
                    {
                        const auto& line = output.Lines[block * C::RingSegments + vertex];
                        const auto& next = output.Lines[block * C::RingSegments + (vertex + 1) % C::RingSegments];
                        EXPECT_TRUE(line.RoundCaps);
                        EXPECT_TRUE(line.ValidJoins());
                        EXPECT_NEAR(std::hypot(line.StartJoinNormal.NormalX(), line.StartJoinNormal.NormalY()), 1.f, .0001f);
                        EXPECT_EQ(line.EndJoinNormal.x, next.StartJoinNormal.x);
                        EXPECT_EQ(line.EndJoinNormal.y, next.StartJoinNormal.y);
                        EXPECT_EQ(line.B.x, next.A.x);
                        EXPECT_EQ(line.B.y, next.A.y);
                        EXPECT_EQ(line.B.z, next.A.z);
                    }
                }
                const auto count = hits.Count;
                ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
                EXPECT_EQ(hits.Count, count);
                EXPECT_FALSE(output.DecorationsOmitted);
            }
        }
    }
}

TEST(GizmoRotationFrameTest, PickedRingPointReprojectsToTheChosenVisibleChordWithPerspectiveCorrection)
{
    auto input = Input();
    input.Mode = Operation::Rotate;
    FlyCamera camera(4.f / 3.f, {});
    camera.SetPosition({7.f, 5.f, 10.f});
    camera.SetOrientation(25.f, -35.f);
    input.Camera          = camera.CaptureFrameData();
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    const auto projection = Projection(input);
    for (uint32_t i = 0; i < hits.Count; ++i)
    {
        const auto& shape = hits.Shapes[i];
        const auto  point = (shape.Points[0] + shape.Points[1]) * .5f;
        Vec3f       world = {};
        ASSERT_TRUE(hits.PickRingPoint(Handle(shape.Handle), point, projection, world));
        const auto screen = Screen(input, world);
        EXPECT_NEAR(screen.x, point.x, .001f);
        EXPECT_NEAR(screen.y, point.y, .001f);
    }
    Vec3f preserved(77.f, 88.f, 99.f);
    EXPECT_FALSE(hits.PickRingPoint(Handle::RotateX, {-10000.f, -10000.f}, projection, preserved));
    EXPECT_FALSE(hits.PickRingPoint(Handle::TranslateX, {}, projection, preserved));
    EXPECT_EQ(preserved.x, 77.f);
    ASSERT_GT(hits.Count, 0u);
    const auto first_shape = hits.Shapes[0];
    const auto first_span  = hits.RingSpans[0];
    const auto point       = (first_shape.Points[0] + first_shape.Points[1]) * .5f;
    hits.Count             = 1;
    for (uint32_t invalid = 0; invalid < 10; ++invalid)
    {
        hits.Shapes[0]    = first_shape;
        hits.RingSpans[0] = first_span;
        auto& shape       = hits.Shapes[0];
        switch (invalid)
        {
            case 0:
                shape.Padding = -1.f;
                break;
            case 1:
                shape.Padding = std::numeric_limits<float>::infinity();
                break;
            case 2:
                shape.Padding = std::numeric_limits<float>::quiet_NaN();
                break;
            case 3:
                shape.Depth = 0.f;
                break;
            case 4:
                shape.Depth = std::numeric_limits<float>::infinity();
                break;
            case 5:
                shape.Geometry = HitGeometry::Rectangle;
                break;
            case 6:
                shape.Priority = HitPriority::ViewRing;
                break;
            case 7:
                shape.Points[0].x = std::numeric_limits<float>::quiet_NaN();
                break;
            case 8:
                shape.Points[1].y = std::numeric_limits<float>::infinity();
                break;
            case 9:
                hits.RingSpans[0].A.z = std::numeric_limits<float>::quiet_NaN();
                break;
        }
        EXPECT_FALSE(hits.PickRingPoint(Handle(first_shape.Handle), point, projection, preserved)) << invalid;
        EXPECT_EQ(preserved.x, 77.f);
        EXPECT_EQ(preserved.y, 88.f);
        EXPECT_EQ(preserved.z, 99.f);
    }
    hits.Count = HitFrame::MaxShapes + 1;
    EXPECT_FALSE(hits.PickRingPoint(Handle::RotateX, {}, projection, preserved));
    EXPECT_EQ(preserved.z, 99.f);
}

TEST(GizmoRotationFrameTest, WedgeHasSeamFreeFillDashedStartAndSolidActualCurrentEdge)
{
    for (double angle : {std::numbers::pi / 2.0, -std::numbers::pi / 2.0, 5.0 * std::numbers::pi / 2.0})
    {
        auto input     = Input();
        input.Mode     = Operation::Rotate;
        input.Active   = Handle::RotateZ;
        input.Rotation = {
            .Axis = {0.f, 0.f, 1.f},
              .StartDirection = {1.f, 0.f, 0.f},
              .Angle = angle, .Visible = true
        };
        GizmoFrameData output = {};
        HitFrame       hits   = {};
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        EXPECT_EQ(output.TriangleCount, std::abs(angle) > 2.0 * std::numbers::pi ? C::RingSegments : C::RingSegments / 4);
        for (uint32_t i = 0; i < output.TriangleCount; ++i)
        {
            EXPECT_EQ(output.Triangles[i].Color, Packed(UI::Tokens::AccentBlue, C::RotationWedgeAlpha));
            EXPECT_EQ(output.Triangles[i].AntiAliasEdges, 1u);
        }
        const auto& start   = output.Lines[output.LineCount - 2];
        const auto& current = output.Lines[output.LineCount - 1];
        EXPECT_EQ(start.DashOn, C::GhostDashOnPx);
        EXPECT_EQ(start.DashOff, C::GhostDashOffPx);
        EXPECT_EQ(current.DashOn, 0.f);
        EXPECT_EQ(current.Width, C::RotationWedgeWidthPx);
        const auto center = Screen(input, input.Pivot), end = Screen(input, current.B);
        EXPECT_NEAR(end.x, center.x, .001f);
        EXPECT_NEAR(end.y - center.y, angle > 0.0 ? -C::RingRadiusPx : C::RingRadiusPx, .001f);
    }
}

TEST(GizmoRotationFrameTest, MalformedOptionalWedgeIsOmittedWithoutHidingOrUnpickingHandles)
{
    auto input     = Input();
    input.Mode     = Operation::Rotate;
    input.Active   = Handle::RotateZ;
    input.Rotation = {
        .Axis = {0.f, 0.f, 1.f},
          .StartDirection = {1.f, 0.f, 0.f},
          .Angle = std::numeric_limits<double>::quiet_NaN(), .Visible = true
    };
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_TRUE(output.DecorationsOmitted);
    EXPECT_EQ(output.TriangleCount, 0u);
    EXPECT_EQ(output.LineCount, 6u * C::RingSegments);
    EXPECT_GT(hits.Count, 0u);
    EXPECT_EQ(hits.Pick({547.f, 361.f}).Handle, uint32_t(Handle::RotateView));
}

TEST(GizmoRotationFrameTest, ClippedPoseSweepStaysBoundedAndPublishesOnlyVisibleRingHits)
{
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    for (bool ortho : {false, true})
    {
        for (bool reverse : {false, true})
        {
            for (float distance : {.2f, .5f, 2.f, 20.f, 1100.f})
            {
                auto input     = Input(ortho, reverse, distance);
                input.Mode     = Operation::Rotate;
                input.Active   = Handle::RotateX;
                input.Rotation = {
                    .Axis = {1.f, 0.f, 0.f},
                      .StartDirection = {0.f, 0.f, 1.f},
                      .Angle = 12.3, .Visible = true
                };
                const auto result = BuildFrame(input, output, hits);
                ASSERT_NE(result, FrameBuildResult::Invalid);
                ASSERT_TRUE(output.ValidCounts());
                ASSERT_LE(hits.Count, HitFrame::MaxShapes);
                for (uint32_t i = 0; i < hits.Count; ++i)
                {
                    for (const auto& point : {hits.RingSpans[i].A, hits.RingSpans[i].B})
                    {
                        G::ProjectedPoint projected = {};
                        ASSERT_TRUE(G::Project(point, Projection(input), input.Viewport, &projected));
                        EXPECT_GT(projected.ClipW, 0.f);
                        EXPECT_GE(projected.Depth, -.0001f);
                        EXPECT_LE(projected.Depth, 1.0001f);
                    }
                }
            }
        }
    }
}

TEST(GizmoRotationFrameTest, RepeatedBuildsResetOnlyLivePrefixesIncludingRingHitSpans)
{
    auto input                                  = Input();
    input.Mode                                  = Operation::Rotate;
    GizmoFrameData output                       = {};
    HitFrame       hits                         = {};
    hits.RingSpans[HitFrame::MaxShapes - 1].A.x = 77.f;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        EXPECT_EQ(output.LineCount, 5u * C::RingSegments);
        EXPECT_EQ(hits.RingSpans[HitFrame::MaxShapes - 1].A.x, 77.f);
    }
    input.Mode = Operation::Select;
    EXPECT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Hidden);
    ExpectCleared(output, hits);
    EXPECT_EQ(hits.RingSpans[HitFrame::MaxShapes - 1].A.x, 77.f);
}

TEST(GizmoPivotFrameTest, MarkerMatchesLogicalSizesColorsAndDashesWithoutAddingHitShapes)
{
    GizmoFrameData plain = {}, decorated = {};
    HitFrame       plain_hits = {}, hits = {};
    for (const auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        for (bool ortho : {false, true})
        {
            for (bool reverse : {false, true})
            {
                for (uint32_t height : {600u, 1200u})
                {
                    auto input  = Input(ortho, reverse, 10.f, height);
                    input.Mode  = mode;
                    input.Pivot = {1.f, 0.f, 0.f};
                    ASSERT_EQ(BuildFrame(input, plain, plain_hits), FrameBuildResult::Built);
                    input.PivotMarker = {.Origin = {}, .Visible = true};
                    ASSERT_EQ(BuildFrame(input, decorated, hits), FrameBuildResult::Built);
                    EXPECT_EQ(hits.Count, plain_hits.Count);
                    EXPECT_EQ(decorated.LineCount, plain.LineCount + 7u);
                    EXPECT_EQ(decorated.SpriteCount, plain.SpriteCount + 1u);
                    const float dpi  = float(height) / 600.f;
                    const auto& ring = decorated.Sprites[decorated.SpriteCount - 1];
                    EXPECT_EQ(ring.Size, C::PivotRingSizePx * dpi);
                    EXPECT_EQ(ring.OutlineWidth, C::PivotRingWidthPx * dpi);
                    EXPECT_EQ(ring.Fill, 0u);
                    EXPECT_EQ(ring.Outline, Packed(UI::Tokens::AccentOrange));
                    EXPECT_TRUE(ring.Round);
                    const auto* lines = decorated.Lines + plain.LineCount;
                    EXPECT_EQ(lines[0].Width, C::PivotLinkWidthPx * dpi);
                    EXPECT_EQ(lines[0].Color, Packed(UI::Tokens::TextPrimary, C::PivotLinkAlpha));
                    EXPECT_EQ(lines[0].DashOn, 4.f * dpi);
                    EXPECT_EQ(lines[0].DashOff, 3.f * dpi);
                    for (uint32_t i : {1u, 2u})
                    {
                        EXPECT_EQ(lines[i].Width, C::PivotCrossWidthPx * dpi);
                        EXPECT_EQ(lines[i].Color, Packed(UI::Tokens::AccentOrange));
                        const auto delta = Screen(input, lines[i].B) - Screen(input, lines[i].A);
                        EXPECT_NEAR(std::hypot(delta.x, delta.y), C::PivotCrossSizePx, .002f);
                    }
                    for (uint32_t i = 3; i < 7; ++i)
                    {
                        const auto delta = Screen(input, lines[i].B) - Screen(input, lines[i].A);
                        EXPECT_NEAR(std::hypot(delta.x, delta.y), C::PivotOriginSizePx, .002f);
                        EXPECT_EQ(lines[i].DashOn, 4.f * dpi);
                        EXPECT_EQ(lines[i].DashOff, 3.f * dpi);
                        EXPECT_EQ(lines[i].DashPhase, C::PivotOriginSizePx * (i - 3) * dpi);
                    }
                    for (const auto& shape : plain_hits.View())
                    {
                        EXPECT_EQ(hits.Pick(shape.Points[0]).Handle, plain_hits.Pick(shape.Points[0]).Handle);
                    }
                }
            }
        }
    }
}

TEST(GizmoPivotFrameTest, RotateCentreIsPickableOnlyForExplicitPivotEditing)
{
    auto input            = Input();
    input.Mode            = Operation::Rotate;
    GizmoFrameData output = {};
    HitFrame       hits   = {};
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    const auto count = hits.Count;
    // Edge-on axis rings can cross the centre; ordinary rotation still has
    // no centre-priority handle of its own.
    EXPECT_EQ(Find(hits, Handle::PivotEdit, HitPriority::Center), nullptr);
    input.PivotEditable = true;
    ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
    EXPECT_EQ(hits.Count, count + 1u);
    EXPECT_EQ(hits.Pick(Screen(input, input.Pivot)).Handle, uint32_t(Handle::PivotEdit));
}

TEST(GizmoPivotFrameTest, MalformedOptionalOriginCannotInvalidateTransformHandles)
{
    for (const auto mode : {Operation::Translate, Operation::Rotate, Operation::Scale})
    {
        auto input           = Input();
        input.Mode           = mode;
        GizmoFrameData plain = {}, output = {};
        HitFrame       plain_hits = {}, hits = {};
        ASSERT_EQ(BuildFrame(input, plain, plain_hits), FrameBuildResult::Built);
        input.PivotMarker = {
            .Origin = {std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f},
              .Visible = true
        };
        ASSERT_EQ(BuildFrame(input, output, hits), FrameBuildResult::Built);
        EXPECT_TRUE(output.DecorationsOmitted);
        EXPECT_TRUE(output.ValidCounts());
        EXPECT_EQ(hits.Count, plain_hits.Count);
        EXPECT_EQ(output.SpriteCount, plain.SpriteCount + 1u);
    }
}
