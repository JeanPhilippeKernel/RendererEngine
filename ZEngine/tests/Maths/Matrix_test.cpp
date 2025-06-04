#include <Core/Maths/Matrix.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

constexpr float EPSILON = 1e-5f;

// ========== CONSTRUCTION TESTS ==========

TEST(MatrixTest, DefaultConstruction)
{
    Mat2f m2;
    Mat3f m3;
    Mat4f m4;

    // Default construction should zero-initialize
    for (size_t i = 0; i < 2; ++i)
    {
        for (size_t j = 0; j < 2; ++j)
        {
            EXPECT_NEAR(m2(i, j), 0.0f, EPSILON);
        }
    }

    for (size_t i = 0; i < 3; ++i)
    {
        for (size_t j = 0; j < 3; ++j)
        {
            EXPECT_NEAR(m3(i, j), 0.0f, EPSILON);
        }
    }

    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_NEAR(m4(i, j), 0.0f, EPSILON);
        }
    }
}

TEST(MatrixTest, ParameterConstruction)
{
    // Mat2 construction
    Mat2f m2(1.1f, 2.2f, 3.3f, 4.4f);
    EXPECT_NEAR(m2(0, 0), 1.1f, EPSILON);
    EXPECT_NEAR(m2(0, 1), 2.2f, EPSILON);
    EXPECT_NEAR(m2(1, 0), 3.3f, EPSILON);
    EXPECT_NEAR(m2(1, 1), 4.4f, EPSILON);

    // Mat3 construction
    Mat3f m3(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f);
    EXPECT_NEAR(m3(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(m3(0, 1), 2.0f, EPSILON);
    EXPECT_NEAR(m3(0, 2), 3.0f, EPSILON);
    EXPECT_NEAR(m3(1, 0), 4.0f, EPSILON);
    EXPECT_NEAR(m3(1, 1), 5.0f, EPSILON);
    EXPECT_NEAR(m3(1, 2), 6.0f, EPSILON);
    EXPECT_NEAR(m3(2, 0), 7.0f, EPSILON);
    EXPECT_NEAR(m3(2, 1), 8.0f, EPSILON);
    EXPECT_NEAR(m3(2, 2), 9.0f, EPSILON);

    // Mat4 construction
    Mat4f m4(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    EXPECT_NEAR(m4(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(m4(0, 1), 5.0f, EPSILON);
    EXPECT_NEAR(m4(3, 3), 16.0f, EPSILON);
}

TEST(MatrixTest, VectorConstruction)
{
    Vec2<float> v1(1.0f, 2.0f);
    Vec2<float> v2(3.0f, 4.0f);
    Mat2f       m2(v1, v2);

    EXPECT_NEAR(m2(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(m2(1, 0), 2.0f, EPSILON);
    EXPECT_NEAR(m2(0, 1), 3.0f, EPSILON);
    EXPECT_NEAR(m2(1, 1), 4.0f, EPSILON);

    Vec3<float> v3a(1.0f, 2.0f, 3.0f);
    Vec3<float> v3b(4.0f, 5.0f, 6.0f);
    Vec3<float> v3c(7.0f, 8.0f, 9.0f);
    Mat3f       m3(v3a, v3b, v3c);

    EXPECT_NEAR(m3(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(m3(1, 0), 2.0f, EPSILON);
    EXPECT_NEAR(m3(2, 0), 3.0f, EPSILON);
}

TEST(MatrixTest, CopyConstruction)
{
    Mat2f original(1.0f, 2.0f, 3.0f, 4.0f);
    Mat2f copy(original);

    for (size_t i = 0; i < 2; ++i)
    {
        for (size_t j = 0; j < 2; ++j)
        {
            EXPECT_NEAR(copy(i, j), original(i, j), EPSILON);
        }
    }
}

// ========== INDEXING TESTS ==========

TEST(MatrixTest, IndexingOperators)
{
    Mat3f        m3(1, 2, 3, 4, 5, 6, 7, 8, 9);

    // Test column access operator for Mat3
    Vec3<float>& col0 = m3[0];
    EXPECT_NEAR(col0.x, 1.0f, EPSILON);
    EXPECT_NEAR(col0.y, 4.0f, EPSILON);
}

// ========== ARITHMETIC TESTS ==========

TEST(MatrixTest, Addition)
{
    Mat2f m1(1, 2, 3, 4);
    Mat2f m2(5, 6, 7, 8);
    Mat2f result = m1 + m2;

    EXPECT_NEAR(result(0, 0), 6.0f, EPSILON);
    EXPECT_NEAR(result(0, 1), 8.0f, EPSILON);
    EXPECT_NEAR(result(1, 0), 10.0f, EPSILON);
    EXPECT_NEAR(result(1, 1), 12.0f, EPSILON);

    // Test compound assignment
    Mat2f m3(1, 2, 3, 4);
    m3 += m2;
    EXPECT_NEAR(m3(0, 0), 6.0f, EPSILON);
    EXPECT_NEAR(m3(1, 1), 12.0f, EPSILON);
}

TEST(MatrixTest, Subtraction)
{
    Mat2f m1(5, 6, 7, 8);
    Mat2f m2(1, 2, 3, 4);
    Mat2f result = m1 - m2;

    EXPECT_NEAR(result(0, 0), 4.0f, EPSILON);
    EXPECT_NEAR(result(0, 1), 4.0f, EPSILON);
    EXPECT_NEAR(result(1, 0), 4.0f, EPSILON);
    EXPECT_NEAR(result(1, 1), 4.0f, EPSILON);

    Mat2f m3(5, 6, 7, 8);
    m3 -= m2;
    EXPECT_NEAR(m3(0, 0), 4.0f, EPSILON);
    EXPECT_NEAR(m3(1, 1), 4.0f, EPSILON);
}

TEST(MatrixTest, ScalarMultiplication)
{
    Mat2f m(1, 2, 3, 4);
    Mat2f result = m * 2.0f;

    EXPECT_NEAR(result(0, 0), 2.0f, EPSILON);
    EXPECT_NEAR(result(0, 1), 4.0f, EPSILON);
    EXPECT_NEAR(result(1, 0), 6.0f, EPSILON);
    EXPECT_NEAR(result(1, 1), 8.0f, EPSILON);

    Mat2f m2(1, 2, 3, 4);
    m2 *= 3.0f;
    EXPECT_NEAR(m2(0, 0), 3.0f, EPSILON);
    EXPECT_NEAR(m2(1, 1), 12.0f, EPSILON);
}

TEST(MatrixTest, ScalarDivision)
{
    Mat2f m(2, 4, 6, 8);
    Mat2f result = m / 2.0f;

    EXPECT_NEAR(result(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(result(0, 1), 2.0f, EPSILON);
    EXPECT_NEAR(result(1, 0), 3.0f, EPSILON);
    EXPECT_NEAR(result(1, 1), 4.0f, EPSILON);

    Mat2f m2(4, 8, 12, 16);
    m2 /= 4.0f;
    EXPECT_NEAR(m2(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(m2(1, 1), 4.0f, EPSILON);
}

TEST(MatrixTest, MatrixMultiplication)
{
    Mat3f m1(1, 2, 3, 0, 1, 4, 5, 6, 0);
    Mat3f m2     = Identity<Mat3f>();

    Mat3f result = m1 * m2;

    // Multiplying by identity should result in the original matrix
    for (size_t i = 0; i < 3; ++i)
    {
        for (size_t j = 0; j < 3; ++j)
        {
            EXPECT_NEAR(result(i, j), m1(i, j), EPSILON);
        }
    }

    // Test specific multiplication
    Mat3f a = Identity<Mat3f>();
    Mat3f b(2, 0, 0, 0, 2, 0, 0, 0, 2);
    Mat3f product = a * b;

    EXPECT_NEAR(product(0, 0), 2.0f, EPSILON);
    EXPECT_NEAR(product(1, 1), 2.0f, EPSILON);
    EXPECT_NEAR(product(2, 2), 2.0f, EPSILON);
}

// ========== DETERMINANT TESTS ==========

TEST(MatrixTest, Determinant2x2)
{
    Mat2f m1(1, 2, 3, 4);
    EXPECT_NEAR(Determinant(m1), -2.0f, EPSILON);

    Mat2f m2(2, 0, 0, 2);
    EXPECT_NEAR(Determinant(m2), 4.0f, EPSILON);

    Mat2f m3 = Identity<Mat2f>();
    EXPECT_NEAR(Determinant(m3), 1.0f, EPSILON);
}

TEST(MatrixTest, Determinant3x3)
{
    Mat3f m1(1, 2, 3, 0, 1, 4, 5, 6, 0);
    EXPECT_NEAR(Determinant(m1), 1.0f, EPSILON);

    Mat3f identity = Identity<Mat3f>();
    EXPECT_NEAR(Determinant(identity), 1.0f, EPSILON);

    Mat3f singular(1, 2, 3, 2, 4, 6, 3, 6, 9); // Singular matrix
    EXPECT_NEAR(Determinant(singular), 0.0f, EPSILON);
}

TEST(MatrixTest, Determinant4x4)
{
    Mat4f identity = Identity<Mat4f>();
    EXPECT_NEAR(Determinant(identity), 1.0f, EPSILON);

    Mat4f m(2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2);
    EXPECT_NEAR(Determinant(m), 16.0f, EPSILON); // 2^4 = 16
}

// ========== IDENTITY MATRIX TESTS ==========

TEST(MatrixTest, IdentityMatrices)
{
    Mat2f id2 = Identity<Mat2f>();
    EXPECT_NEAR(id2(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(id2(0, 1), 0.0f, EPSILON);
    EXPECT_NEAR(id2(1, 0), 0.0f, EPSILON);
    EXPECT_NEAR(id2(1, 1), 1.0f, EPSILON);

    Mat3f id3 = Identity<Mat3f>();
    for (size_t i = 0; i < 3; ++i)
    {
        for (size_t j = 0; j < 3; ++j)
        {
            if (i == j)
            {
                EXPECT_NEAR(id3(i, j), 1.0f, EPSILON);
            }
            else
            {
                EXPECT_NEAR(id3(i, j), 0.0f, EPSILON);
            }
        }
    }

    Mat4f id4 = Identity<Mat4f>();
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            if (i == j)
            {
                EXPECT_NEAR(id4(i, j), 1.0f, EPSILON);
            }
            else
            {
                EXPECT_NEAR(id4(i, j), 0.0f, EPSILON);
            }
        }
    }
}

// ========== INVERSE TESTS ==========

TEST(MatrixTest, Inverse3x3)
{
    Mat3f m(1, 0, 2, 2, 1, 0, 1, 1, 1);
    Mat3f inv     = m.Inverse();
    Mat3f product = m * inv;
    Mat3f identity = Identity<Mat3f>();

    // Check if (m * m^-1) ≈ I
    for (size_t i = 0; i < 3; ++i)
    {
        for (size_t j = 0; j < 3; ++j)
        {
            EXPECT_NEAR(product(i, j), identity(i, j), EPSILON);
        }
    }
}

// ========== EDGE CASES AND ERROR CONDITIONS ==========

TEST(MatrixTest, ZeroDivision)
{
    Mat2f m(1, 2, 3, 4);
    Mat2f result = m / 0.0f;

    // Check for infinity or very large values
    EXPECT_TRUE(std::isinf(result(0, 0)) || std::abs(result(0, 0)) > 1e30f);
}

TEST(MatrixTest, IntegerMatrices)
{
    IMat2 m(1, 2, 3, 4);
    EXPECT_EQ(m(0, 0), 1);
    EXPECT_EQ(m(0, 1), 2);
    EXPECT_EQ(m(1, 0), 3);
    EXPECT_EQ(m(1, 1), 4);

    IMat2 m2(5, 6, 7, 8);
    IMat2 sum = m + m2;
    EXPECT_EQ(sum(0, 0), 6);
    EXPECT_EQ(sum(1, 1), 12);
}

TEST(MatrixTest, LargeValueHandling)
{
    Mat2f m(1e20f, 1e20f, 1e20f, 1e20f);
    Mat2f result = m * 2.0f;

    EXPECT_NEAR(result(0, 0), 2e20f, 1e15f);
    EXPECT_NEAR(result(1, 1), 2e20f, 1e15f);
}

TEST(MatrixTest, SmallValueHandling)
{
    Mat2f m(1e-20f, 1e-20f, 1e-20f, 1e-20f);
    Mat2f result = m * 2.0f;

    EXPECT_NEAR(result(0, 0), 2e-20f, 1e-25f);
    EXPECT_NEAR(result(1, 1), 2e-20f, 1e-25f);
}

TEST(MatrixTest, ChainedOperations)
{
    Mat3f a = Identity<Mat3f>();
    Mat3f b(2, 0, 0, 0, 2, 0, 0, 0, 2);
    Mat3f c(1, 1, 1, 1, 1, 1, 1, 1, 1);

    // Test: (a * b) + c - a
    Mat3f result = ((a * b) + c) - a;

    EXPECT_NEAR(result(0, 0), 2.0f, EPSILON); // (1*2) + 1 - 1 = 2
    EXPECT_NEAR(result(0, 1), 1.0f, EPSILON); // (0*0) + 1 - 0 = 1
    EXPECT_NEAR(result(1, 1), 2.0f, EPSILON); // (1*2) + 1 - 1 = 2
}

TEST(MatrixTest, ConstructionAndIndexingFloat)
{
    Mat2<float> m2(1.1f, 2.2f, 3.3f, 4.4f);
    EXPECT_NEAR(m2(0, 0), 1.1f, EPSILON);
    EXPECT_NEAR(m2(1, 0), 3.3f, EPSILON);
    EXPECT_NEAR(m2(0, 1), 2.2f, EPSILON);
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
    auto        d = Determinant(m3);
    EXPECT_NEAR(Determinant(m3), 1.0f, EPSILON);
}
