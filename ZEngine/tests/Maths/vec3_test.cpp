#include <Core/Maths/Vec3.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

TEST(Vec3Test, ConstructorAndAccess)
{
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(Vec3Test, Addition)
{
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(3.0f, 4.0f, 5.0f);
    Vec3 result = a + b;

    EXPECT_FLOAT_EQ(result.x, 4.0f);
    EXPECT_FLOAT_EQ(result.y, 6.0f);
    EXPECT_FLOAT_EQ(result.z, 8.0f);
}

TEST(Vec3Test, Subtraction)
{
    Vec3 a(5.0f, 6.0f, 7.0f);
    Vec3 b(2.0f, 4.0f, 1.0f);
    Vec3 result = a - b;

    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 6.0f);
}

TEST(Vec3Test, ScalarMultiplication)
{
    Vec3 v(2.0f, -3.0f, 4.0f);
    Vec3 result = v * 2.0f;

    EXPECT_FLOAT_EQ(result.x, 4.0f);
    EXPECT_FLOAT_EQ(result.y, -6.0f);
    EXPECT_FLOAT_EQ(result.z, 8.0f);
}

TEST(Vec3Test, ScalarDivision)
{
    Vec3 v(6.0f, 4.0f, 2.0f);
    Vec3 result = v / 2.0f;

    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 1.0f);
}

TEST(Vec3Test, LengthAndNormalize)
{
    Vec3 v(1.0f, 2.0f, 2.0f); // length = 3

    EXPECT_FLOAT_EQ(v.length(), 3.0f);

    Vec3 norm = v.normalized();
    EXPECT_NEAR(norm.length(), 1.0f, 1e-5f);
    EXPECT_NEAR(norm.x, 1.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(norm.y, 2.0f / 3.0f, 1e-5f);
    EXPECT_NEAR(norm.z, 2.0f / 3.0f, 1e-5f);
}

TEST(Vec3Test, DotProduct)
{
    Vec3  a(1.0f, 2.0f, 3.0f);
    Vec3  b(4.0f, -5.0f, 6.0f);

    float dot = Vec3::dot(a, b);
    EXPECT_FLOAT_EQ(dot, 12.0f); // 1*4 + 2*(-5) + 3*6 = 4 -10 + 18 = 12
}

TEST(Vec3Test, CrossProduct)
{
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(0.0f, 1.0f, 0.0f);

    Vec3 cross = Vec3::cross(a, b);

    EXPECT_FLOAT_EQ(cross.x, 0.0f);
    EXPECT_FLOAT_EQ(cross.y, 0.0f);
    EXPECT_FLOAT_EQ(cross.z, 1.0f); // a × b = (0, 0, 1)
}

TEST(Vec3Test, Equality)
{
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(1.0f, 2.0f, 3.0f);
    Vec3 c(2.0f, 3.0f, 4.0f);

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}
