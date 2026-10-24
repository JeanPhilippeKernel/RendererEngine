#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/Rendering/Renderers/GizmoPrimitiveBuilder.h>
#include <gtest/gtest.h>
#include <limits>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::Rendering::Renderers;
namespace G  = ZEngine::Core::Maths::Geometry;
using Result = GizmoEmitResult;

namespace
{
    G::ProjectionFrame Frame(bool reverse = false)
    {
        ZEngine::Rendering::Cameras::FlyCamera camera(4.f / 3.f, {});
        camera.SetPosition({0.f, 0.f, 10.f});
        camera.SetOrientation(0.f, 0.f);
        auto captured = camera.CaptureFrameData();
        if (reverse)
            for (uint32_t i = 0; i < 4; ++i)
                captured.Projection(2, i) = captured.Projection(3, i) - captured.Projection(2, i);
        G::ProjectionFrame frame = {};
        EXPECT_TRUE(G::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, G::ProjectionKind::Perspective, reverse, &frame));
        return frame;
    }
    G::ProjectionFrame DepthFrame(bool ortho, bool reverse)
    {
        using namespace ZEngine::Rendering::Cameras;
        CameraSetting settings = {};
        settings.NearPlane     = 0.1f;
        settings.FarPlane      = 10.f;
        FlyCamera camera(4.f / 3.f, settings);
        camera.SetPosition({});
        camera.SetOrientation(0.f, 0.f);
        if (ortho)
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        auto captured = camera.CaptureFrameData();
        if (reverse)
            for (uint32_t i = 0; i < 4; ++i)
                captured.Projection(2, i) = captured.Projection(3, i) - captured.Projection(2, i);
        G::ProjectionFrame frame = {};
        EXPECT_TRUE(G::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, ortho ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &frame));
        return frame;
    }
    void NearVisible(const G::ProjectionFrame& frame, const Vec3f& point)
    {
        G::ProjectedPoint projected = {};
        ASSERT_TRUE(
            G::Project(
                point,
                frame,
                {
                {},
                {800.f, 600.f}
        },
                &projected));
        EXPECT_GT(projected.ClipW, 0.f);
        if (frame.ReverseZ)
            EXPECT_LE(projected.Depth, 1.f + 0.0001f);
        else
            EXPECT_GE(projected.Depth, -0.0001f);
    }
} // namespace

TEST(GizmoPrimitiveBuilderTest, PixelSizesAreConvertedExactlyOnceWithoutChangingWorldPositions)
{
    const auto            frame  = Frame();
    GizmoFrameData        output = {};
    GizmoPrimitiveBuilder builder(output, frame, 600.f, 1600, 1200);
    ASSERT_TRUE(builder.Valid());
    ASSERT_EQ(
        builder.Line({
        .A = {1.f, 2.f, 0.f},
          .B = {2.f, 3.f, 0.f},
          .Width = 2.5f, .DashOn = 4.f, .DashOff = 3.f, .DashPhase = 7.f
    }),
        Result::Emitted);
    ASSERT_EQ(builder.Sprite({.Size = 12.f, .OutlineWidth = 1.5f}), Result::Emitted);
    EXPECT_EQ(output.Lines[0].A.x, 1.f);
    EXPECT_EQ(output.Lines[0].Width, 5.f);
    EXPECT_EQ(output.Lines[0].DashOn, 8.f);
    EXPECT_EQ(output.Lines[0].DashOff, 6.f);
    EXPECT_EQ(output.Lines[0].DashPhase, 14.f);
    EXPECT_EQ(output.Sprites[0].Size, 24.f);
    EXPECT_EQ(output.Sprites[0].OutlineWidth, 3.f);
}

TEST(GizmoPrimitiveBuilderTest, JoinedStrokesRetainPixelNormalsButRetireOnlyDepthClippedEndpointOwnership)
{
    for (bool ortho : {false, true})
        for (bool reverse : {false, true})
        {
            GizmoFrameData        output = {};
            GizmoPrimitiveBuilder builder(output, DepthFrame(ortho, reverse), 600.f, 1600, 1200);
            for (uint32_t configuration = 0; configuration < 5; ++configuration)
            {
                output.Reset();
                const float     a    = configuration == 0 ? -1.f : configuration == 1 || configuration == 4 ? -.05f : -20.f;
                const float     b    = configuration == 3 ? -.05f : configuration == 4 ? -20.f : -2.f;
                const GizmoLine line = {
                    .A = {-1.f, 0.f, a},
                      .B = { 1.f, 0.f, b},
                      .Width = 3.5f, .RoundCaps = true, .StartJoinNormal = GizmoJoinNormal::FromUnit(1.0, 0.0), .EndJoinNormal = GizmoJoinNormal::FromUnit(.6, .8)
                };
                ASSERT_EQ(builder.Line(line), Result::Emitted);
                ASSERT_EQ(output.LineCount, 1u);
                EXPECT_EQ(output.Lines[0].Width, 7.f);
                EXPECT_EQ(output.Lines[0].StartJoinNormal.NormalX(), configuration == 0 ? 1.f : 0.f);
                EXPECT_NEAR(output.Lines[0].EndJoinNormal.NormalX(), configuration >= 3 ? 0.f : .6f, .0001f);
                EXPECT_TRUE(output.Lines[0].ValidJoins());
            }
            GizmoLine malformed = {
                .Width = 2.f, .RoundCaps = true, .StartJoinNormal = {1, 0}
            };
            const auto before = output.LineCount;
            EXPECT_EQ(builder.Line(malformed), Result::Invalid);
            EXPECT_EQ(output.LineCount, before);
        }
}

TEST(GizmoPrimitiveBuilderTest, LinesCrossingTheEyeAreClippedBeforePublishingBothDepthConventions)
{
    for (bool reverse : {false, true})
    {
        const auto            frame  = Frame(reverse);
        GizmoFrameData        output = {};
        GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
        ASSERT_EQ(
            builder.Line({
            .A = {0.f, 0.f, 11.f},
              .B = {1.f, 0.f,  0.f},
              .Width = 2.f
        }),
            Result::Emitted);
        ASSERT_EQ(output.LineCount, 1u);
        EXPECT_NEAR(output.Lines[0].A.z, 9.9f, 0.0001f);
        EXPECT_EQ(output.Lines[0].B.x, 1.f);
        NearVisible(frame, output.Lines[0].A);
        NearVisible(frame, output.Lines[0].B);
        ASSERT_EQ(
            builder.Line({
            .A = {1.f, 0.f,  0.f},
              .B = {0.f, 0.f, 11.f},
              .Width = 2.f
        }),
            Result::Emitted);
        EXPECT_NEAR(output.Lines[1].B.z, 9.9f, 0.0001f);
    }
}

TEST(GizmoPrimitiveBuilderTest, LeadingNearClipKeepsTheOriginalProjectedDashRhythm)
{
    const auto            frame  = Frame();
    GizmoFrameData        output = {};
    GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
    const GizmoLine       line = {
        .A = {0.1f, 0.f, 9.95f},
          .B = { 1.f, 0.f,   0.f},
          .Width = 2.f, .DashOn = 4.f, .DashOff = 3.f, .DashPhase = 5.f
    };
    ASSERT_EQ(builder.Line(line), Result::Emitted);
    G::ProjectedPoint original = {}, clipped = {};
    ASSERT_TRUE(
        G::Project(
            line.A,
            frame,
            {
            {},
            {800.f, 600.f}
    },
            &original));
    ASSERT_TRUE(
        G::Project(
            output.Lines[0].A,
            frame,
            {
            {},
            {800.f, 600.f}
    },
            &clipped));
    EXPECT_NEAR(output.Lines[0].DashPhase, 5.f + std::abs(original.Screen.x - clipped.Screen.x), 0.001f);
}

TEST(GizmoPrimitiveBuilderTest, TrianglesClipToAtMostTwoTrianglesWithoutNonpositiveClipW)
{
    for (bool reverse : {false, true})
    {
        const auto            frame  = Frame(reverse);
        GizmoFrameData        output = {};
        GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
        ASSERT_EQ(
            builder.Triangle({
            .A = {0.f, 0.f, 11.f},
              .B = {1.f, 0.f,  0.f},
              .C = {0.f, 1.f,  0.f},
              .Color = 53
        }),
            Result::Emitted);
        ASSERT_EQ(output.TriangleCount, 2u);
        EXPECT_EQ(output.Triangles[0].AntiAliasEdges, 5u);
        EXPECT_EQ(output.Triangles[1].AntiAliasEdges, 3u);
        for (uint32_t i = 0; i < output.TriangleCount; ++i)
        {
            const auto& triangle = output.Triangles[i];
            NearVisible(frame, triangle.A);
            NearVisible(frame, triangle.B);
            NearVisible(frame, triangle.C);
            EXPECT_EQ(triangle.Color, 53u);
        }
        output.Reset();
        ASSERT_EQ(
            builder.Triangle({
            .A = {0.f, 0.f, 11.f},
              .B = {1.f, 0.f, 11.f},
              .C = {0.f, 1.f,  0.f}
        }),
            Result::Emitted);
        EXPECT_EQ(output.TriangleCount, 1u);
    }
}

TEST(GizmoPrimitiveBuilderTest, BoundaryCoverageMasksPreserveSharedPlaneEdgesAfterClipping)
{
    const auto            frame  = Frame();
    GizmoFrameData        output = {};
    GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
    ASSERT_EQ(
        builder.Triangle({
        .A = {0.f, 0.f, 0.f},
          .B = {1.f, 0.f, 0.f},
          .C = {0.f, 1.f, 0.f},
          .AntiAliasEdges = 5
    }),
        Result::Emitted);
    EXPECT_EQ(output.Triangles[0].AntiAliasEdges, 5u);
    output.Reset();
    ASSERT_EQ(
        builder.Triangle({
        .A = {0.f, 0.f, 11.f},
          .B = {1.f, 0.f,  0.f},
          .C = {0.f, 1.f,  0.f},
          .AntiAliasEdges = 0
    }),
        Result::Emitted);
    ASSERT_EQ(output.TriangleCount, 2u);
    EXPECT_EQ(output.Triangles[0].AntiAliasEdges, 0u);
    EXPECT_EQ(output.Triangles[1].AntiAliasEdges, 2u); // new near boundary only
}

TEST(GizmoPrimitiveBuilderTest, FullyHiddenGeometryDoesNotConsumeCapacity)
{
    const auto            frame  = Frame();
    GizmoFrameData        output = {};
    GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
    EXPECT_EQ(
        builder.Line({
        .A = {0.f, 0.f, 11.f},
          .B = {1.f, 0.f, 11.f},
          .Width = 2.f
    }),
        Result::Culled);
    EXPECT_EQ(
        builder.Triangle({
        .A = {0.f, 0.f, 11.f},
          .B = {1.f, 0.f, 11.f},
          .C = {0.f, 1.f, 11.f}
    }),
        Result::Culled);
    EXPECT_EQ(
        builder.Sprite({
        .Position = {0.f, 0.f, 9.99f},
          .Size = 12.f
    }),
        Result::Culled);
    EXPECT_TRUE(output.Empty());
}

TEST(GizmoPrimitiveBuilderTest, OverflowDoesNotPartiallyEmitAClippedTriangle)
{
    const auto            frame  = Frame();
    GizmoFrameData        output = {};
    GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
    output.TriangleCount                         = GizmoFrameData::MaxTriangles - 1;
    output.Triangles[output.TriangleCount].Color = 79;
    EXPECT_EQ(
        builder.Triangle({
        .A = {0.f, 0.f, 11.f},
          .B = {1.f, 0.f,  0.f},
          .C = {0.f, 1.f,  0.f}
    }),
        Result::CapacityExceeded);
    EXPECT_EQ(output.TriangleCount, GizmoFrameData::MaxTriangles - 1);
    EXPECT_EQ(output.Triangles[output.TriangleCount].Color, 79u);
    output.LineCount = GizmoFrameData::MaxLines;
    EXPECT_EQ(builder.Line({.Width = 2.f}), Result::CapacityExceeded);
    output.SpriteCount = GizmoFrameData::MaxSprites;
    EXPECT_EQ(builder.Sprite({.Size = 12.f}), Result::CapacityExceeded);
}

TEST(GizmoPrimitiveBuilderTest, InvalidInputsAndOverflowLeaveCountsAndStorageUnchanged)
{
    const auto            frame  = Frame();
    GizmoFrameData        output = {};
    GizmoPrimitiveBuilder builder(output, frame, 600.f, 1600, 1200);
    const float           nan = std::numeric_limits<float>::quiet_NaN();
    output.Lines[0].Color     = 31;
    EXPECT_EQ(builder.Line({.Width = 0.f}), Result::Invalid);
    EXPECT_EQ(
        builder.Line({
        .A = {nan, 0.f, 0.f},
          .Width = 2.f
    }),
        Result::Invalid);
    EXPECT_EQ(builder.Line({.Width = std::numeric_limits<float>::max()}), Result::Invalid);
    EXPECT_EQ(
        builder.Triangle({
        .A = {0.f, nan, 0.f}
    }),
        Result::Invalid);
    EXPECT_EQ(builder.Sprite({.Size = 12.f, .OutlineWidth = 7.f}), Result::Invalid);
    EXPECT_EQ(builder.Sprite({.Size = std::numeric_limits<float>::max()}), Result::Invalid);
    EXPECT_TRUE(output.Empty());
    EXPECT_EQ(output.Lines[0].Color, 31u);
    GizmoPrimitiveBuilder invalid(output, frame, 0.f, 800, 600);
    EXPECT_FALSE(invalid.Valid());
    EXPECT_EQ(invalid.Line({.Width = 2.f}), Result::Invalid);
}

TEST(GizmoPrimitiveBuilderTest, FarClippingMatchesBothDepthConventionsAndProjectionKinds)
{
    GizmoFrameData output = {};
    for (bool ortho : {false, true})
        for (bool reverse : {false, true})
        {
            output.Reset();
            const auto            frame = DepthFrame(ortho, reverse);
            GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
            ASSERT_EQ(
                builder.Line({
                .A = {0.f, 0.f,  -2.f},
                  .B = {1.f, 0.f, -20.f},
                  .Width = 2.f
            }),
                Result::Emitted);
            EXPECT_NEAR(output.Lines[0].B.z, -10.f, 0.0001f);
            EXPECT_EQ(
                builder.Sprite({
                .Position = {0.f, 0.f, -20.f},
                  .Size = 10.f
            }),
                Result::Culled);
            EXPECT_EQ(
                builder.Line({
                .A = {0.f, 0.f, -20.f},
                  .B = {1.f, 0.f, -30.f},
                  .Width = 2.f
            }),
                Result::Culled);
            EXPECT_EQ(
                builder.Triangle({
                .A = {0.f, 0.f, -20.f},
                  .B = {1.f, 0.f, -20.f},
                  .C = {0.f, 1.f, -20.f}
            }),
                Result::Culled);
            EXPECT_EQ(output.LineCount, 1u);
            EXPECT_EQ(output.TriangleCount, 0u);
            EXPECT_EQ(output.SpriteCount, 0u);
        }
}

TEST(GizmoPrimitiveBuilderTest, BothDepthPlanesCanProduceThreeTrianglesWithoutInternalAaSeams)
{
    GizmoFrameData output = {};
    for (bool ortho : {false, true})
        for (bool reverse : {false, true})
        {
            output.Reset();
            const auto            frame = DepthFrame(ortho, reverse);
            GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
            ASSERT_EQ(
                builder.Triangle({
                .A = {-0.2f,   0.f, -0.05f},
                  .B = { 0.2f, -0.2f,   -2.f},
                  .C = {  0.f,  0.2f,  -20.f}
            }),
                Result::Emitted);
            ASSERT_EQ(output.TriangleCount, 3u);
            uint32_t boundary_edges = 0;
            for (uint32_t i = 0; i < output.TriangleCount; ++i)
            {
                const auto& triangle = output.Triangles[i];
                for (uint32_t bit = 0; bit < 3; ++bit)
                    boundary_edges += (triangle.AntiAliasEdges >> bit) & 1u;
                for (const auto& point : {triangle.A, triangle.B, triangle.C})
                {
                    G::ProjectedPoint projected = {};
                    ASSERT_TRUE(
                        G::Project(
                            point,
                            frame,
                            {
                            {},
                            {800.f, 600.f}
                    },
                            &projected));
                    EXPECT_GE(projected.Depth, -0.0001f);
                    EXPECT_LE(projected.Depth, 1.0001f);
                }
            }
            EXPECT_EQ(boundary_edges, 5u);
            output.Reset();
            output.TriangleCount                         = GizmoFrameData::MaxTriangles - 2;
            output.Triangles[output.TriangleCount].Color = 0x12345678u;
            EXPECT_EQ(
                builder.Triangle({
                .A = {-0.2f,   0.f, -0.05f},
                  .B = { 0.2f, -0.2f,   -2.f},
                  .C = {  0.f,  0.2f,  -20.f}
            }),
                Result::CapacityExceeded);
            EXPECT_EQ(output.TriangleCount, GizmoFrameData::MaxTriangles - 2);
            EXPECT_EQ(output.Triangles[output.TriangleCount].Color, 0x12345678u);
        }
}

TEST(GizmoPrimitiveBuilderTest, LeadingFarClipPreservesDashPhase)
{
    const auto            frame  = DepthFrame(false, false);
    GizmoFrameData        output = {};
    GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
    const GizmoLine       line = {
        .A = {1.f, 0.f, -20.f},
          .B = {0.f, 0.f,  -2.f},
          .Width = 2.f, .DashOn = 4.f, .DashOff = 3.f, .DashPhase = 5.f
    };
    ASSERT_EQ(builder.Line(line), Result::Emitted);
    G::ProjectedPoint original = {}, clipped = {};
    ASSERT_TRUE(
        G::Project(
            line.A,
            frame,
            {
            {},
            {800.f, 600.f}
    },
            &original));
    ASSERT_TRUE(
        G::Project(
            output.Lines[0].A,
            frame,
            {
            {},
            {800.f, 600.f}
    },
            &clipped));
    EXPECT_NEAR(output.Lines[0].A.z, -10.f, 0.0001f);
    EXPECT_NEAR(output.Lines[0].DashPhase, 5.f + std::abs(original.Screen.x - clipped.Screen.x), 0.0001f);
}

TEST(GizmoPrimitiveBuilderTest, InfiniteFarProjectionDoesNotInventAFarCutoff)
{
    GizmoFrameData output = {};
    for (bool reverse : {false, true})
    {
        output.Reset();
        Mat4f projection = Identity<Mat4f>();
        projection(1, 1) = -1.f;
        projection(2, 2) = -1.f;
        projection(2, 3) = -0.1f;
        projection(3, 2) = -1.f;
        projection(3, 3) = 0.f;
        if (reverse)
            for (uint32_t i = 0; i < 4; ++i)
                projection(2, i) = projection(3, i) - projection(2, i);
        G::ProjectionFrame frame = {};
        ASSERT_TRUE(G::MakeProjectionFrame(Identity<Mat4f>(), projection, {}, G::ProjectionKind::Perspective, reverse, &frame));
        GizmoPrimitiveBuilder builder(output, frame, 600.f, 800, 600);
        ASSERT_EQ(
            builder.Line({
            .A = {0.f, 0.f, -1e8f},
              .B = {1.f, 0.f, -1e9f},
              .Width = 2.f
        }),
            Result::Emitted);
        EXPECT_EQ(output.Lines[0].A.z, -1e8f);
        EXPECT_EQ(output.Lines[0].B.z, -1e9f);
        EXPECT_EQ(
            builder.Sprite({
            .Position = {0.f, 0.f, -1e9f},
              .Size = 10.f
        }),
            Result::Emitted);
    }
}
