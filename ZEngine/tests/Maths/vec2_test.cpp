#include <Core/Maths/Vec2.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

TEST(Vec2Test, ConstructorAndAccess)
{
    Vec2 v(1.0f, 2.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
}

TEST(Vec2Test, Addition)
{
    Vec2 a(1.0f, 2.0f);
    Vec2 b(3.0f, 4.0f);
    Vec2 result = a + b;

    EXPECT_FLOAT_EQ(result.x, 4.0f);
    EXPECT_FLOAT_EQ(result.y, 6.0f);
}

TEST(Vec2Test, Subtraction)
{
    Vec2 a(5.0f, 6.0f);
    Vec2 b(2.0f, 4.0f);
    Vec2 result = a - b;

    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
}

TEST(Vec2Test, ScalarMultiplication)
{
    Vec2 v(2.0f, -3.0f);
    Vec2 result = v * 2.0f;

    EXPECT_FLOAT_EQ(result.x, 4.0f);
    EXPECT_FLOAT_EQ(result.y, -6.0f);
}

TEST(Vec2Test, ScalarDivision)
{
    Vec2 v(6.0f, 4.0f);
    Vec2 result = v / 2.0f;

    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
}

TEST(Vec2Test, LengthAndNormalize)
{
    Vec2 v(3.0f, 4.0f);

    EXPECT_FLOAT_EQ(v.length(), 5.0f);

    Vec2 norm = v.normalized();
    EXPECT_NEAR(norm.length(), 1.0f, 1e-5f);
    EXPECT_NEAR(norm.x, 0.6f, 1e-5f);
    EXPECT_NEAR(norm.y, 0.8f, 1e-5f);
}

TEST(Vec2Test, DotProduct)
{
    Vec2  a(1.0f, 2.0f);
    Vec2  b(3.0f, 4.0f);

    float dot = Vec2::dot(a, b);
    EXPECT_FLOAT_EQ(dot, 11.0f);
}

TEST(Vec2Test, CrossProduct2D)
{
    Vec2  a(1.0f, 2.0f);
    Vec2  b(3.0f, 4.0f);

    float cross = Vec2::cross(a, b);
    EXPECT_FLOAT_EQ(cross, -2.0f);
}

TEST(Vec2Test, Equality)
{
    Vec2 a(1.0f, 2.0f);
    Vec2 b(1.0f, 2.0f);
    Vec2 c(2.0f, 3.0f);

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}
