#include <Core/Maths/IVec2.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

TEST(IVec2Test, ConstructorAndAccess)
{
    IVec2 v(1, 2);
    EXPECT_FLOAT_EQ(v.x, 1);
    EXPECT_FLOAT_EQ(v.y, 2);
}

TEST(IVec2Test, Addition)
{
    IVec2 a(1, 2);
    IVec2 b(3, 4);
    IVec2 result = a + b;

    EXPECT_FLOAT_EQ(result.x, 4);
    EXPECT_FLOAT_EQ(result.y, 6);
}

TEST(IVec2Test, Subtraction)
{
    IVec2 a(5, 6);
    IVec2 b(2, 4);
    IVec2 result = a - b;

    EXPECT_FLOAT_EQ(result.x, 3);
    EXPECT_FLOAT_EQ(result.y, 2);
}

TEST(IVec2Test, ScalarMultiplication)
{
    IVec2 v(2, -3);
    IVec2 result = v * 2;

    EXPECT_FLOAT_EQ(result.x, 4);
    EXPECT_FLOAT_EQ(result.y, -6);
}

TEST(IVec2Test, ScalarDivision)
{
    IVec2 v(6, 4);
    IVec2 result = v / 2;

    EXPECT_FLOAT_EQ(result.x, 3);
    EXPECT_FLOAT_EQ(result.y, 2);
}

TEST(IVec2Test, DotProduct)
{
    IVec2 a(1, 2);
    IVec2 b(3, 4);

    int   dot = IVec2::dot(a, b);
    EXPECT_FLOAT_EQ(dot, 11);
}

TEST(IVec2Test, CrossProduct2D)
{
    IVec2 a(1, 2);
    IVec2 b(3, 4);

    int   cross = IVec2::cross(a, b);
    EXPECT_FLOAT_EQ(cross, -2);
}

TEST(IVec2Test, Equality)
{
    IVec2 a(1, 2);
    IVec2 b(1, 2);
    IVec2 c(2, 3);

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}
