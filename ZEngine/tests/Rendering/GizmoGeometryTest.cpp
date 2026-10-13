#include <ZEngine/Core/Maths/Vec.h>
#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>

namespace ShaderGeometry
{
    using uint = uint32_t;
    using vec3 = ZEngine::Core::Maths::Vec3f;
    using std::cos;
    using std::sin;
    // Compile and exercise the shader's actual vertex generator, not a replica.
#include "../../../Resources/Shaders/editor_gizmo_ring.glsl"
} // namespace ShaderGeometry

TEST(GizmoGeometryTest, EveryRotationRingTriangleHasAreaAndFacesItsAxis)
{
    using namespace ZEngine::Core::Maths;
    for (uint32_t triangle = 0; triangle < 96; ++triangle)
    {
        const Vec3f a      = ShaderGeometry::ringVertex(triangle * 3);
        const Vec3f b      = ShaderGeometry::ringVertex(triangle * 3 + 1);
        const Vec3f c      = ShaderGeometry::ringVertex(triangle * 3 + 2);
        const Vec3f normal = cross3d(b - a, c - a);
        EXPECT_GT(normal.x, 0.001f) << "triangle " << triangle;
        EXPECT_FLOAT_EQ(normal.y, 0.f);
        EXPECT_FLOAT_EQ(normal.z, 0.f);
        EXPECT_FLOAT_EQ(a.x, 0.f);
        EXPECT_NEAR(a.magnitude(), 0.79f, 1.e-6f);
    }
}

TEST(GizmoGeometryTest, RotationRingSegmentsShareEdgesAndCloseWithoutGaps)
{
    for (uint32_t segment = 0; segment < 48; ++segment)
    {
        const auto end  = ShaderGeometry::ringVertex(segment * 6 + 5);
        const auto next = ShaderGeometry::ringVertex(((segment + 1) % 48) * 6 + 1);
        EXPECT_NEAR(end.x, next.x, 1.e-6f);
        EXPECT_NEAR(end.y, next.y, 1.e-6f);
        EXPECT_NEAR(end.z, next.z, 1.e-6f);
    }
}
