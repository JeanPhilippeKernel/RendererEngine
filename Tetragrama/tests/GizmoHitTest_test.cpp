#include <Tetragrama/Gizmo/GizmoConstants.h>
#include <Tetragrama/Gizmo/GizmoHitTest.h>
#include <gtest/gtest.h>
#include <limits>

using namespace Tetragrama::Gizmo;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;

namespace
{
    HitShape Segment(uint32_t handle = 1, HitPriority priority = HitPriority::AxisLine, float depth = 10.f)
    {
        HitShape shape  = {.Handle = handle, .Priority = priority, .Depth = depth, .Padding = Constants::LineHitTolerancePx};
        shape.Points[0] = {0.f, 0.f};
        shape.Points[1] = {120.f, 0.f};
        return shape;
    }

    HitShape Box(uint32_t handle = 1, HitPriority priority = HitPriority::Center, float depth = 10.f)
    {
        HitShape shape  = {.Handle = handle, .Priority = priority, .Geometry = HitGeometry::Rectangle, .Depth = depth, .Padding = Constants::BoxHitPaddingPx};
        shape.Points[0] = {-5.f, -5.f};
        shape.Points[1] = {5.f, 5.f};
        return shape;
    }

    HitShape Quad()
    {
        HitShape shape  = {.Handle = 1, .Priority = HitPriority::Plane, .Geometry = HitGeometry::ConvexQuad, .Depth = 10.f, .Padding = Constants::PlaneHitPaddingPx};
        shape.Points[0] = {0.f, 0.f};
        shape.Points[1] = {20.f, 10.f};
        shape.Points[2] = {20.f, 30.f};
        shape.Points[3] = {0.f, 20.f};
        return shape;
    }

    HitResult Pick(const HitShape& shape, const Vec2f& point)
    {
        return HitTest({&shape, 1}, point);
    }
} // namespace

TEST(GizmoHitTestTest, DefaultsAndEmptyViewsNeverHit)
{
    EXPECT_EQ(HitTest({}, {}).Handle, 0u);
    EXPECT_EQ(HitTest({nullptr, 1}, {}).Handle, 0u);
    const HitShape shape = {};
    EXPECT_EQ(Pick(shape, {}).Handle, 0u);
}

TEST(GizmoHitTestTest, SegmentUsesEightLogicalPixelsIncludingRoundEndpoints)
{
    const auto shape = Segment();
    EXPECT_EQ(Pick(shape, {60.f, Constants::LineHitTolerancePx}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {60.f, Constants::LineHitTolerancePx + 0.01f}).Handle, 0u);
    EXPECT_EQ(Pick(shape, {-Constants::LineHitTolerancePx, 0.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {-Constants::LineHitTolerancePx - 0.01f, 0.f}).Handle, 0u);
    EXPECT_EQ(Pick(shape, {125.f, 5.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {128.f, 8.f}).Handle, 0u);
}

TEST(GizmoHitTestTest, DiagonalSegmentDoesNotUseItsBoundingRectangle)
{
    auto shape      = Segment();
    shape.Points[1] = {120.f, 120.f};
    EXPECT_EQ(Pick(shape, {60.f, 60.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {10.f, 100.f}).Handle, 0u);
}

TEST(GizmoHitTestTest, ZeroLengthSegmentHasOnlyItsCircularTolerance)
{
    auto shape      = Segment();
    shape.Points[1] = shape.Points[0];
    EXPECT_EQ(Pick(shape, {0.f, 8.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {8.f, 8.f}).Handle, 0u);
}

TEST(GizmoHitTestTest, CentreAndCubeRectanglesUseHalfSizePlusFourPixels)
{
    const auto shape = Box();
    EXPECT_EQ(Pick(shape, {9.f, 9.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {9.01f, 0.f}).Handle, 0u);
    EXPECT_EQ(Pick(shape, {0.f, -9.01f}).Handle, 0u);
}

TEST(GizmoHitTestTest, ProjectedPlaneUsesTheQuadNotItsBoundingRectangle)
{
    const auto shape = Quad();
    EXPECT_EQ(Pick(shape, {10.f, 15.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {19.f, 1.f}).Handle, 0u);
    EXPECT_EQ(Pick(shape, {-2.f, 10.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {-2.01f, 10.f}).Handle, 0u);
    EXPECT_EQ(Pick(shape, {10.f, 3.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {10.f, 2.5f}).Handle, 0u);
}

TEST(GizmoHitTestTest, PlaneAcceptsEitherWinding)
{
    auto       shape = Quad();
    const auto next  = shape.Points[1];
    shape.Points[1]  = shape.Points[3];
    shape.Points[3]  = next;
    EXPECT_EQ(Pick(shape, {10.f, 15.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {-2.f, 10.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {19.f, 1.f}).Handle, 0u);
}

TEST(GizmoHitTestTest, InvalidAndDegeneratePolygonsDoNotCapture)
{
    auto shape      = Quad();
    shape.Points[3] = shape.Points[0];
    EXPECT_EQ(Pick(shape, {0.f, 0.f}).Handle, 0u);
    shape           = Quad();
    shape.Points[2] = {1.f, 2.f};
    EXPECT_EQ(Pick(shape, {0.f, 0.f}).Handle, 0u);
    shape           = Quad();
    const auto next = shape.Points[1];
    shape.Points[1] = shape.Points[2];
    shape.Points[2] = next;
    EXPECT_EQ(Pick(shape, {0.f, 0.f}).Handle, 0u);
    shape           = Box();
    shape.Points[1] = shape.Points[0];
    EXPECT_EQ(Pick(shape, {}).Handle, 0u);
}

TEST(GizmoHitTestTest, ClippedTriangleUsesItsOutlineNotItsBoundingRectangle)
{
    auto shape      = Quad();
    shape.Geometry  = HitGeometry::Triangle;
    shape.Points[0] = {0.f, 0.f};
    shape.Points[1] = {20.f, 0.f};
    shape.Points[2] = {0.f, 20.f};
    EXPECT_EQ(Pick(shape, {5.f, 5.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {19.f, 19.f}).Handle, 0u);
    EXPECT_EQ(Pick(shape, {-2.f, 5.f}).Handle, 1u);
    EXPECT_EQ(Pick(shape, {-2.01f, 5.f}).Handle, 0u);
    const auto p    = shape.Points[1];
    shape.Points[1] = shape.Points[2];
    shape.Points[2] = p;
    EXPECT_EQ(Pick(shape, {5.f, 5.f}).Handle, 1u);
    shape.Points[2] = shape.Points[1];
    EXPECT_EQ(Pick(shape, {0.f, 0.f}).Handle, 0u);
}

TEST(GizmoHitTestTest, EveryHigherPriorityHandleBeatsLowerPriorityRegardlessOfDepth)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(HitPriority::Component); ++i)
    {
        for (uint32_t j = i + 1; j <= static_cast<uint32_t>(HitPriority::Component); ++j)
        {
            const HitShape shapes[] = {Segment(1, static_cast<HitPriority>(i), 100.f), Segment(2, static_cast<HitPriority>(j), 1.f)};
            EXPECT_EQ(HitTest(shapes, {60.f, 0.f}).Handle, 1u);
            const HitShape reversed[] = {shapes[1], shapes[0]};
            EXPECT_EQ(HitTest(reversed, {60.f, 0.f}).Handle, 1u);
        }
    }
}

TEST(GizmoHitTestTest, EqualPriorityChoosesNearestCameraDepthAndThenStableId)
{
    HitShape shapes[] = {Segment(9, HitPriority::AxisLine, 10.f), Segment(2, HitPriority::AxisLine, 20.f)};
    EXPECT_EQ(HitTest(shapes, {60.f, 0.f}).Handle, 9u);
    shapes[1].Depth = 10.f;
    const auto hit  = HitTest(shapes, {60.f, 0.f});
    EXPECT_EQ(hit.Handle, 2u);
    EXPECT_EQ(hit.Priority, HitPriority::AxisLine);
    EXPECT_FLOAT_EQ(hit.Depth, 10.f);
    const HitShape reversed[] = {shapes[1], shapes[0]};
    EXPECT_EQ(HitTest(reversed, {60.f, 0.f}).Handle, 2u);
}

TEST(GizmoHitTestTest, MatchingActiveHandleBeatsAllOtherPriorities)
{
    const HitShape shapes[] = {Box(1), Segment(2, HitPriority::ViewRing, 100.f)};
    EXPECT_EQ(HitTest(shapes, {}, 2).Handle, 2u);
    EXPECT_EQ(HitTest(shapes, {}, 99).Handle, 1u);
    EXPECT_EQ(HitTest(shapes, {300.f, 300.f}, 2).Handle, 0u);
}

TEST(GizmoHitTestTest, MultipleSegmentsCanRepresentTheSameHandle)
{
    HitShape shapes[]   = {Segment(1, HitPriority::AxisRing), Segment(1, HitPriority::AxisRing), Segment(2, HitPriority::ViewRing)};
    shapes[1].Points[0] = {120.f, 0.f};
    shapes[1].Points[1] = {120.f, 120.f};
    EXPECT_EQ(HitTest(shapes, {120.f, 60.f}).Handle, 1u);
    EXPECT_EQ(HitTest(shapes, {60.f, 0.f}).Handle, 1u);
}

TEST(GizmoHitTestTest, MalformedShapeMetadataNeverCaptures)
{
    for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -1.f})
    {
        auto shape  = Segment();
        shape.Depth = value;
        EXPECT_EQ(Pick(shape, {}).Handle, 0u);
        shape         = Segment();
        shape.Padding = value;
        EXPECT_EQ(Pick(shape, {}).Handle, 0u);
    }
    auto shape   = Segment();
    shape.Handle = 0;
    EXPECT_EQ(Pick(shape, {}).Handle, 0u);
    shape       = Segment();
    shape.Depth = 0.f;
    EXPECT_EQ(Pick(shape, {}).Handle, 0u);
    shape          = Segment();
    shape.Priority = static_cast<HitPriority>(255);
    EXPECT_EQ(Pick(shape, {}).Handle, 0u);
    shape          = Segment();
    shape.Geometry = static_cast<HitGeometry>(255);
    EXPECT_EQ(Pick(shape, {}).Handle, 0u);
}

TEST(GizmoHitTestTest, NonFinitePointsNeverCapture)
{
    for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
    {
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            Vec2f point = {};
            point[axis] = value;
            EXPECT_EQ(Pick(Segment(), point).Handle, 0u);
            for (uint32_t vertex = 0; vertex < 4; ++vertex)
            {
                auto shape                 = Quad();
                shape.Points[vertex][axis] = value;
                EXPECT_EQ(Pick(shape, {}).Handle, 0u);
            }
        }
    }
}

TEST(GizmoHitTestTest, InvalidPreferredShapeCannotHideAValidLowerPriorityCandidate)
{
    HitShape shapes[]     = {Box(1), Segment(2)};
    shapes[0].Points[1].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(HitTest(shapes, {}).Handle, 2u);
}
