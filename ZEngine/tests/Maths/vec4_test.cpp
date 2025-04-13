#include <Core/Maths/Vec4.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

TEST(Vec4Test, ConstructorAndAccess)
{
    Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
    EXPECT_FLOAT_EQ(v.w, 4.0f);
}

TEST(Vec4Test, UnaryMinus)
{
    Vec4 v(1.0f, -2.0f, 3.0f, -4.0f);
    Vec4 result = -v;

    EXPECT_FLOAT_EQ(result.x, -1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, -3.0f);
    EXPECT_FLOAT_EQ(result.w, 4.0f);
}

TEST(Vec4Test, Addition)
{
    Vec4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4 b(5.0f, 6.0f, 7.0f, 8.0f);
    Vec4 result = a + b;

    EXPECT_FLOAT_EQ(result.x, 6.0f);
    EXPECT_FLOAT_EQ(result.y, 8.0f);
    EXPECT_FLOAT_EQ(result.z, 10.0f);
    EXPECT_FLOAT_EQ(result.w, 12.0f);
}

TEST(Vec4Test, Subtraction)
{
    Vec4 a(5.0f, 6.0f, 7.0f, 8.0f);
    Vec4 b(2.0f, 1.0f, 3.0f, 4.0f);
    Vec4 result = a - b;

    EXPECT_FLOAT_EQ(result.x, 3.0f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
    EXPECT_FLOAT_EQ(result.z, 4.0f);
    EXPECT_FLOAT_EQ(result.w, 4.0f);
}

TEST(Vec4Test, ScalarMultiplication)
{
    Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4 result = v * 2.0f;

    EXPECT_FLOAT_EQ(result.x, 2.0f);
    EXPECT_FLOAT_EQ(result.y, 4.0f);
    EXPECT_FLOAT_EQ(result.z, 6.0f);
    EXPECT_FLOAT_EQ(result.w, 8.0f);
}

TEST(Vec4Test, ScalarDivision)
{
    Vec4 v(2.0f, 4.0f, 6.0f, 8.0f);
    Vec4 result = v / 2.0f;

    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
    EXPECT_FLOAT_EQ(result.w, 4.0f);
}

TEST(Vec4Test, LengthAndNormalize)
{
    Vec4 v(1.0f, 2.0f, 2.0f, 1.0f);
    Vec4 norm = v.normalized();

    EXPECT_NEAR(norm.length(), 1.0f, 1e-5f);

    EXPECT_NEAR(norm.x, 1.0f / std::sqrt(10.0f), 1e-5f);
    EXPECT_NEAR(norm.y, 2.0f / std::sqrt(10.0f), 1e-5f);
    EXPECT_NEAR(norm.z, 2.0f / std::sqrt(10.0f), 1e-5f);
    EXPECT_NEAR(norm.w, 1.0f / std::sqrt(10.0f), 1e-5f);
}

TEST(Vec4Test, DotProduct)
{
    Vec4  a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4  b(5.0f, 6.0f, 7.0f, 8.0f);

    float dot = Vec4::dot(a, b);
    EXPECT_FLOAT_EQ(dot, 70.0f);
}

TEST(Vec4Test, Equality)
{
    Vec4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4 b(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4 c(4.0f, 3.0f, 2.0f, 1.0f);

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}
