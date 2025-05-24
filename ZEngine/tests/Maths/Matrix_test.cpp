#include <Core/Maths/Matrix.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

constexpr float EPSILON = 1e-5f;

TEST(MatrixTest, ConstructionAndIndexingFloat)
{
    Mat2<float> m2(1.1f, 2.2f, 3.3f, 4.4f);
    EXPECT_NEAR(m2(0, 0), 1.1f, EPSILON);
    EXPECT_NEAR(m2(1, 0), 2.2f, EPSILON);
    EXPECT_NEAR(m2(0, 1), 3.3f, EPSILON);
    EXPECT_NEAR(m2(1, 1), 4.4f, EPSILON);

    Mat3<float> m3(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f);
    EXPECT_NEAR(m3(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(m3(0, 1), 2.0f, EPSILON);
    EXPECT_NEAR(m3(0, 2), 3.0f, EPSILON);
    EXPECT_NEAR(m3(1, 0), 4.0f, EPSILON);
    EXPECT_NEAR(m3(1, 1), 5.0f, EPSILON);
    EXPECT_NEAR(m3(1, 2), 6.0f, EPSILON);
    EXPECT_NEAR(m3(2, 0), 7.0f, EPSILON);
    EXPECT_NEAR(m3(2, 1), 8.0f, EPSILON);
    EXPECT_NEAR(m3(2, 2), 9.0f, EPSILON);
}

TEST(MatrixTest, DeterminantFloat)
{
    Mat2<float> m2(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_NEAR(Determinant(m2), -2.0f, EPSILON);

    Mat3<float> m3(1, 2, 3, 0, 1, 4, 5, 6, 0);
    EXPECT_NEAR(Determinant(m3), 1.0f, EPSILON);
}
