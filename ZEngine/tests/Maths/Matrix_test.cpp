#include <Core/Maths/Matrix.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

constexpr float EPSILON = 1e-5f;

TEST(MatrixTest, ConstructionAndIndexingFloat)
{
    Mat2f m2(1.1f, 2.2f, 3.3f, 4.4f);
    EXPECT_NEAR(m2(0, 0), 1.1f, EPSILON);
    EXPECT_NEAR(m2(0,1), 2.2f, EPSILON);
    EXPECT_NEAR(m2(1,0), 3.3f, EPSILON);
    EXPECT_NEAR(m2(1,1), 4.4f, EPSILON);

    Mat3f m3(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f);
    EXPECT_NEAR(m3(0,0), 1.0f, EPSILON);
    EXPECT_NEAR(m3(1,1), 5.0f, EPSILON);
    EXPECT_NEAR(m3(2,2), 9.0f, EPSILON);

    Mat4f m4(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f);
    EXPECT_NEAR(m4(0,0), 1.0f, EPSILON);
    EXPECT_NEAR(m4(1,2), 7.0f, EPSILON);
    EXPECT_NEAR(m4(3,3), 16.0f, EPSILON);
}

TEST(MatrixTest, ArithmeticOpsFloat)
{
    Mat2f a2(1, 2, 3, 4);
    Mat2f b2(0.5f, 0.5f, 0.5f, 0.5f);
    Mat2f c2 = a2 + b2;
    EXPECT_NEAR(c2(0,0), 1.5f, EPSILON);
    EXPECT_NEAR(c2(1,1), 4.5f, EPSILON);

    Mat2f d2 = a2 * 2.0f;
    EXPECT_NEAR(d2(0,1), 4.0f, EPSILON);
    Mat2f e2 = a2 / 2.0f;
    EXPECT_NEAR(e2(1,0), 1.5f, EPSILON);

    Mat3f a3(1, 2, 3, 4, 5, 6, 7, 8, 9);
    Mat3f b3(9, 8, 7, 6, 5, 4, 3, 2, 1);
    Mat3f c3 = a3 + b3;
    EXPECT_NEAR(c3(0,0), 10.0f, EPSILON);
    EXPECT_NEAR(c3(2,2), 10.0f, EPSILON);
}

TEST(MatrixTest, DeterminantFloat)
{
    Mat2f m2(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_NEAR(m2.determinant(), -2.0f, EPSILON);

    Mat3f m3(1, 2, 3, 0, 1, 4, 5, 6, 0);
    EXPECT_NEAR(m3.determinant(), 1.0f, EPSILON);

    Mat4f m4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    EXPECT_NEAR(m4.determinant(), 1.0f, EPSILON);
}

TEST(MatrixTest, Identity)
{
    Mat2f I2 = Identity<Mat2f>();
    Mat3f I3 = Identity<Mat3f>();
    Mat4f I4 = Identity<Mat4f>();

    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            EXPECT_NEAR(I2(i,j), (i == j ? 1.0f : 0.0f), EPSILON);

    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            EXPECT_NEAR(I3(i,j), (i == j ? 1.0f : 0.0f), EPSILON);

    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_NEAR(I4(i,j), (i == j ? 1.0f : 0.0f), EPSILON);
}

TEST(MatrixTest, Inverse)
{
    Mat2f m2(4.0f, 7.0f, 2.0f, 6.0f);
    Mat2f inv2              = m2.Inverse();
    Mat2f shouldBeIdentity2 = m2 * inv2;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            EXPECT_NEAR(shouldBeIdentity2(i,j), (i == j ? 1.0f : 0.0f), EPSILON);

    Mat3f m3(1, 2, 3, 0, 1, 4, 5, 6, 0);
    Mat3f inv3              = m3.Inverse();
    Mat3f shouldBeIdentity3 = m3 * inv3;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            EXPECT_NEAR(shouldBeIdentity3(i,j), (i == j ? 1.0f : 0.0f), EPSILON);

    Mat4f m4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    Mat4f inv4              = m4.Inverse();
    Mat4f shouldBeIdentity4 = m4 * inv4;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_NEAR(shouldBeIdentity4(i,j), (i == j ? 1.0f : 0.0f), EPSILON);
}
