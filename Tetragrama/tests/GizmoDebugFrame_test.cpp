#include <Tetragrama/Gizmo/GizmoConstants.h>
#include <Tetragrama/Gizmo/GizmoDebugFrame.h>
#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Rendering::Renderers;
using namespace Tetragrama::Gizmo;
namespace G = ZEngine::Core::Maths::Geometry;

TEST(GizmoDebugFrameTest, CaptureIncludesExplicitProjectionKind)
{
    FlyCamera camera(4.f / 3.f, {});
    EXPECT_EQ(camera.CaptureFrameData().ProjectionType, CameraType::PERSPECTIVE);
    camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
    EXPECT_EQ(camera.CaptureFrameData().ProjectionType, CameraType::ORTHOGRAPHIC);
}

TEST(GizmoDebugFrameTest, DebugFrameHasAxesArrowheadsContinuousDashedRingAndCube)
{
    FlyCamera camera(4.f / 3.f, {});
    camera.SetPosition({0.f, 0.f, 10.f});
    camera.SetOrientation(0.f, 0.f);
    const auto     captured = camera.CaptureFrameData();
    GizmoFrameData output   = {};
    ASSERT_TRUE(BuildDebugFrame(captured, 600.f, 800, 600, output));
    EXPECT_EQ(output.LineCount, 3u + Constants::RingSegments);
    EXPECT_EQ(output.TriangleCount, 2u); // view-parallel Z has no arrowhead
    EXPECT_EQ(output.SpriteCount, 1u);
    EXPECT_EQ(output.Lines[0].Width, Constants::AxisIdleWidthPx);
    EXPECT_EQ(output.Sprites[0].Size, Constants::ScaleCubeSizePx);
    for (uint32_t i = 3; i < output.LineCount; ++i)
    {
        EXPECT_EQ(output.Lines[i].DashOn, Constants::GhostDashOnPx);
        EXPECT_EQ(output.Lines[i].DashOff, Constants::GhostDashOffPx);
        if (i > 3)
        {
            EXPECT_GT(output.Lines[i].DashPhase, output.Lines[i - 1].DashPhase);
        }
    }
    EXPECT_NE(output.Lines[0].Color, output.Lines[1].Color);
    EXPECT_NE(output.Lines[1].Color, output.Lines[2].Color);
}

TEST(GizmoDebugFrameTest, SamePublishedCameraKeepsOriginAndPixelSizeAtDifferentDepthsAndResolutions)
{
    GizmoFrameData output = {};
    for (bool ortho : {false, true})
    {
        for (float distance : {5.f, 10.f, 100.f})
        {
            for (uint32_t height : {600u, 1200u})
            {
                FlyCamera camera(4.f / 3.f, {});
                camera.SetPosition({0.f, 0.f, distance});
                camera.SetOrientation(0.f, 0.f);
                if (ortho)
                {
                    camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
                }
                const auto     captured = camera.CaptureFrameData();
                const uint32_t width    = height * 4 / 3;
                ASSERT_TRUE(BuildDebugFrame(captured, 600.f, width, height, output));
                G::ProjectionFrame frame = {};
                ASSERT_TRUE(G::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, ortho ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, false, &frame));
                G::ProjectedPoint center = {}, tip = {};
                const G::Viewport viewport = {
                    {},
                    {float(width), float(height)}
                };
                ASSERT_TRUE(G::Project({}, frame, viewport, &center));
                ASSERT_TRUE(G::Project(output.Lines[0].B, frame, viewport, &tip));
                EXPECT_NEAR(center.Screen.x, width * 0.5f, 0.001f);
                EXPECT_NEAR(center.Screen.y, height * 0.5f, 0.001f);
                EXPECT_NEAR(tip.Screen.x - center.Screen.x, Constants::AxisLengthPx * height / 600.f, 0.001f);
                EXPECT_EQ(output.Lines[0].Width, Constants::AxisIdleWidthPx * height / 600.f);
            }
        }
    }
}

TEST(GizmoDebugFrameTest, InvalidOrHiddenCameraClearsPreviouslyVisibleGeometry)
{
    FlyCamera camera(4.f / 3.f, {});
    camera.SetPosition({0.f, 0.f, 10.f});
    camera.SetOrientation(0.f, 0.f);
    GizmoFrameData output = {};
    ASSERT_TRUE(BuildDebugFrame(camera.CaptureFrameData(), 600.f, 800, 600, output));
    EXPECT_FALSE(BuildDebugFrame(camera.CaptureFrameData(), 0.f, 800, 600, output));
    EXPECT_TRUE(output.Empty());
    EXPECT_FALSE(BuildDebugFrame({}, 600.f, 800, 600, output));
    camera.SetOrientation(0.f, 180.f);
    EXPECT_FALSE(BuildDebugFrame(camera.CaptureFrameData(), 600.f, 800, 600, output));
    EXPECT_TRUE(output.Empty());
}
