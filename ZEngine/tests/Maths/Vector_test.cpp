#include <Core/Maths/typedefs.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

TEST(VectorTest, ConstructorAndAccess)
{

    Vec2f v1(1.0f, 2.0f);
    EXPECT_FLOAT_EQ(v1.x, 1.0f);
    EXPECT_FLOAT_EQ(v1.y, 2.0f);

    IVec2 v2(1, 2);
    EXPECT_EQ(v2.x, 1);
    EXPECT_EQ(v2.y, 2);

    Vec3f v3(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v3.x, 1.0f);
    EXPECT_FLOAT_EQ(v3.y, 2.0f);
    EXPECT_FLOAT_EQ(v3.z, 3.0f);

    Vec4f v4(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v4.x, 1.0f);
    EXPECT_FLOAT_EQ(v4.y, 2.0f);
    EXPECT_FLOAT_EQ(v4.z, 3.0f);
    EXPECT_FLOAT_EQ(v4.w, 4.0f);
}

TEST(VectorTest, Addition)
{
    Vec2f v1(1.0f, 2.0f);
    Vec2f a(3.0f, 4.0f);
    Vec2f r1 = v1 + a;
    EXPECT_FLOAT_EQ(r1.x, 4.0f);
    EXPECT_FLOAT_EQ(r1.y, 6.0f);

    IVec2 v2(1, 2);
    IVec2 b(3, 4);
    IVec2 r2 = v2 + b;
    EXPECT_EQ(r2.x, 4);
    EXPECT_EQ(r2.y, 6);

    Vec3f v3(1.0f, 2.0f, 3.0f);
    Vec3f c(3.0f, 4.0f, 5.0f);
    Vec3f r3 = v3 + c;
    EXPECT_FLOAT_EQ(r3.x, 4.0f);
    EXPECT_FLOAT_EQ(r3.y, 6.0f);
    EXPECT_FLOAT_EQ(r3.z, 8.0f);

    Vec4f v4(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4f d(5.0f, 6.0f, 7.0f, 8.0f);
    Vec4f r4 = v4 + d;
    EXPECT_FLOAT_EQ(r4.x, 6.0f);
    EXPECT_FLOAT_EQ(r4.y, 8.0f);
    EXPECT_FLOAT_EQ(r4.z, 10.0f);
    EXPECT_FLOAT_EQ(r4.w, 12.0f);
}

TEST(VectorTest, Subtraction)
{
    Vec2f v1(5.0f, 6.0f);
    Vec2f a(2.0f, 4.0f);
    Vec2f r1 = v1 - a;
    EXPECT_FLOAT_EQ(r1.x, 3.0f);
    EXPECT_FLOAT_EQ(r1.y, 2.0f);

    IVec2 v2(5, 7);
    IVec2 b(2, 4);
    IVec2 r2 = v2 - b;
    EXPECT_EQ(r2.x, 3);
    EXPECT_EQ(r2.y, 3);

    Vec3f v3(5.0f, 6.0f, 7.0f);
    Vec3f c(2.0f, 4.0f, 1.0f);
    Vec3f r3 = v3 - c;
    EXPECT_FLOAT_EQ(r3.x, 3.0f);
    EXPECT_FLOAT_EQ(r3.y, 2.0f);
    EXPECT_FLOAT_EQ(r3.z, 6.0f);

    Vec4f v4(5.0f, 6.0f, 7.0f, 8.0f);
    Vec4f d(2.0f, 1.0f, 3.0f, 4.0f);
    Vec4f r4 = v4 - d;
    EXPECT_FLOAT_EQ(r4.x, 3.0f);
    EXPECT_FLOAT_EQ(r4.y, 5.0f);
    EXPECT_FLOAT_EQ(r4.z, 4.0f);
    EXPECT_FLOAT_EQ(r4.w, 4.0f);
}

TEST(VectorTest, Cross2D)
{
    Vec2f a(1.0f, 2.0f);
    Vec2f b(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(cross2d(a, b), -2.0f);

    IVec2 c(2, 3);
    IVec2 d(4, 5);
    EXPECT_EQ(cross2d(c, d), -2);
}

TEST(VectorTest, Cross3D)
{
    Vec3f a(1.0f, 0.0f, 0.0f);
    Vec3f b(0.0f, 1.0f, 0.0f);

    Vec3f result = cross3d(a, b);
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 1.0f);
}

TEST(VectorTest, ScalarMultiplicationAndDivision)
{
    Vec2f v(2.0f, 4.0f);

    Vec2f scaled = v * 2.0f;
    EXPECT_FLOAT_EQ(scaled.x, 4.0f);
    EXPECT_FLOAT_EQ(scaled.y, 8.0f);

    Vec2f divided = v / 2.0f;
    EXPECT_FLOAT_EQ(divided.x, 1.0f);
    EXPECT_FLOAT_EQ(divided.y, 2.0f);

    IVec2 vi(2, 4);
    IVec2 scaledi = vi * 3;
    EXPECT_EQ(scaledi.x, 6);
    EXPECT_EQ(scaledi.y, 12);

    IVec2 dividedi = vi / 2;
    EXPECT_EQ(dividedi.x, 1);
    EXPECT_EQ(dividedi.y, 2);
}

TEST(VectorTest, DotProduct)
{
    Vec2f a(1.0f, 2.0f);
    Vec2f b(3.0f, 4.0f);

    float result = dot(a, b);
    EXPECT_FLOAT_EQ(result, 11.0f);

    IVec2 ia(2, 3);
    IVec2 ib(4, 5);
    int   iresult = dot(ia, ib);
    EXPECT_EQ(iresult, 23);
}

TEST(VectorTest, Normalized)
{
    Vec2f v(3.0f, 4.0f);
    Vec2f n = v.normalize();

    EXPECT_NEAR(n.x, 0.6f, 1e-5f);
    EXPECT_NEAR(n.y, 0.8f, 1e-5f);
    EXPECT_NEAR(n.magnitude(), 1.0f, 1e-5f);
}