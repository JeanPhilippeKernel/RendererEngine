#include <Core/Maths/typedefs.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::Core::Memory;

class VectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        allocator.Initialize(200);
    }

    void TearDown() override
    {
        allocator.Shutdown();
    }

    ArenaAllocator allocator;
};

TEST_F(VectorTest, ConstructorAndAccess)
{
    Vec2f v1;
    v1.init(&allocator, 1.0f, 2.0f);
    EXPECT_FLOAT_EQ(v1.x(), 1.0f);
    EXPECT_FLOAT_EQ(v1.y(), 2.0f);

    IVec2 v2;
    v2.init(&allocator, 1, 2);
    EXPECT_EQ(v2.x(), 1);
    EXPECT_EQ(v2.y(), 2);

    Vec3f v3;
    v3.init(&allocator, 1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v3.x(), 1.0f);
    EXPECT_FLOAT_EQ(v3.y(), 2.0f);
    EXPECT_FLOAT_EQ(v3.z(), 3.0f);

    Vec4f v4;
    v4.init(&allocator, 1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v4.x(), 1.0f);
    EXPECT_FLOAT_EQ(v4.y(), 2.0f);
    EXPECT_FLOAT_EQ(v4.z(), 3.0f);
    EXPECT_FLOAT_EQ(v4.w(), 4.0f);
}

TEST_F(VectorTest, Addition)
{
    Vec2f v1, a;
    v1.init(&allocator, 1.0f, 2.0f);
    a.init(&allocator, 3.0f, 4.0f);
    Vec2f r1 = v1 + a;
    EXPECT_FLOAT_EQ(r1.x(), 4.0f);
    EXPECT_FLOAT_EQ(r1.y(), 6.0f);

    IVec2 v2, b;
    v2.init(&allocator, 1, 2);
    b.init(&allocator, 3, 4);
    IVec2 r2 = v2 + b;
    EXPECT_EQ(r2.x(), 4);
    EXPECT_EQ(r2.y(), 6);

    Vec3f v3, c;
    v3.init(&allocator, 1.0f, 2.0f, 3.0f);
    c.init(&allocator, 3.0f, 4.0f, 5.0f);
    Vec3f r3 = v3 + c;
    EXPECT_FLOAT_EQ(r3.x(), 4.0f);
    EXPECT_FLOAT_EQ(r3.y(), 6.0f);
    EXPECT_FLOAT_EQ(r3.z(), 8.0f);

    Vec4f v4, d;
    v4.init(&allocator, 1.0f, 2.0f, 3.0f, 4.0f);
    d.init(&allocator, 5.0f, 6.0f, 7.0f, 8.0f);
    Vec4f r4 = v4 + d;
    EXPECT_FLOAT_EQ(r4.x(), 6.0f);
    EXPECT_FLOAT_EQ(r4.y(), 8.0f);
    EXPECT_FLOAT_EQ(r4.z(), 10.0f);
    EXPECT_FLOAT_EQ(r4.w(), 12.0f);
}

TEST_F(VectorTest, Subtraction)
{
    Vec2f v1, a;
    v1.init(&allocator, 5.0f, 6.0f);
    a.init(&allocator, 2.0f, 4.0f);
    Vec2f r1 = v1 - a;
    EXPECT_FLOAT_EQ(r1.x(), 3.0f);
    EXPECT_FLOAT_EQ(r1.y(), 2.0f);

    IVec2 v2, b;
    v2.init(&allocator, 5, 7);
    b.init(&allocator, 2, 4);
    IVec2 r2 = v2 - b;
    EXPECT_EQ(r2.x(), 3);
    EXPECT_EQ(r2.y(), 3);

    Vec3f v3, c;
    v3.init(&allocator, 5.0f, 6.0f, 7.0f);
    c.init(&allocator, 2.0f, 4.0f, 1.0f);
    Vec3f r3 = v3 - c;
    EXPECT_FLOAT_EQ(r3.x(), 3.0f);
    EXPECT_FLOAT_EQ(r3.y(), 2.0f);
    EXPECT_FLOAT_EQ(r3.z(), 6.0f);

    Vec4f v4, d;
    v4.init(&allocator, 5.0f, 6.0f, 7.0f, 8.0f);
    d.init(&allocator, 2.0f, 1.0f, 3.0f, 4.0f);
    Vec4f r4 = v4 - d;
    EXPECT_FLOAT_EQ(r4.x(), 3.0f);
    EXPECT_FLOAT_EQ(r4.y(), 5.0f);
    EXPECT_FLOAT_EQ(r4.z(), 4.0f);
    EXPECT_FLOAT_EQ(r4.w(), 4.0f);
}

TEST_F(VectorTest, Magnitude)
{
    IVec2 v;
    v.init(&allocator, 3, 4);
    EXPECT_NEAR(v.magnitude(), 5.0f, 1e-5f);

    Vec2f v2;
    v2.init(&allocator, 3.0f, 4.0f);
    EXPECT_NEAR(v2.magnitude(), 5.0f, 1e-5f);

    Vec3f v3;
    v3.init(&allocator, 1.0f, 2.0f, 2.0f);
    EXPECT_NEAR(v3.magnitude(), 3.0f, 1e-5f);

    Vec4f v4;
    v4.init(&allocator, 1.0f, 2.0f, 2.0f, 3.0f);
    EXPECT_NEAR(v4.magnitude(), std::sqrt(18.0f), 1e-5f);
}

TEST_F(VectorTest, Cross2D)
{
    Vec2f a, b;
    a.init(&allocator, 1.0f, 2.0f);
    b.init(&allocator, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(a.cross2d(b), -2.0f);

    IVec2 c, d;
    c.init(&allocator, 2, 3);
    d.init(&allocator, 4, 5);
    EXPECT_EQ(c.cross2d(d), -2);
}

TEST_F(VectorTest, Cross3D)
{
    Vec3f a, b;
    a.init(&allocator, 1.0f, 0.0f, 0.0f);
    b.init(&allocator, 0.0f, 1.0f, 0.0f);

    Vec3f result = a.cross3d(b);
    EXPECT_FLOAT_EQ(result.x(), 0.0f);
    EXPECT_FLOAT_EQ(result.y(), 0.0f);
    EXPECT_FLOAT_EQ(result.z(), 1.0f);
}

TEST_F(VectorTest, ScalarMultiplicationAndDivision)
{
    Vec2f v;
    v.init(&allocator, 2.0f, 4.0f);

    Vec2f scaled = v * 2.0f;
    EXPECT_FLOAT_EQ(scaled.x(), 4.0f);
    EXPECT_FLOAT_EQ(scaled.y(), 8.0f);

    Vec2f divided = v / 2.0f;
    EXPECT_FLOAT_EQ(divided.x(), 1.0f);
    EXPECT_FLOAT_EQ(divided.y(), 2.0f);

    IVec2 vi;
    vi.init(&allocator, 2, 4);

    IVec2 scaledi = vi * 3;
    EXPECT_EQ(scaledi.x(), 6);
    EXPECT_EQ(scaledi.y(), 12);

    IVec2 dividedi = vi / 2;
    EXPECT_EQ(dividedi.x(), 1);
    EXPECT_EQ(dividedi.y(), 2);
}
