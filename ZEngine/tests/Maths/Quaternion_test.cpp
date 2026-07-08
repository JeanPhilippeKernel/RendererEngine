#include <ZEngine/Core/Maths/Quaternion.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;

constexpr float EPSILON = 1e-4f;

// ========== CONSTRUCTION TESTS ==========

TEST(QuaternionTest, DefaultConstruction)
{
    Quaternion<float> q;
    EXPECT_NEAR(q.x, 0.0f, EPSILON);
    EXPECT_NEAR(q.y, 0.0f, EPSILON);
    EXPECT_NEAR(q.z, 0.0f, EPSILON);
    EXPECT_NEAR(q.w, 1.0f, EPSILON);

    Quaternion<double> qd;
    EXPECT_NEAR(qd.x, 0.0, 1e-15);
    EXPECT_NEAR(qd.y, 0.0, 1e-15);
    EXPECT_NEAR(qd.z, 0.0, 1e-15);
    EXPECT_NEAR(qd.w, 1.0, 1e-15);

    Quaternion<float> q3(1.0f, 2.0f, 3.0f);
    EXPECT_NEAR(q3.x, 1.0f, EPSILON);
    EXPECT_NEAR(q3.y, 2.0f, EPSILON);
    EXPECT_NEAR(q3.z, 3.0f, EPSILON);
    EXPECT_NEAR(q3.w, 0.0f, EPSILON);
}

TEST(QuaternionTest, Construction)
{
    float             data[] = {1.5f, 2.5f, 3.5f, 4.5f};
    Quaternion<float> q(data);
    EXPECT_NEAR(q.x, 1.5f, EPSILON);
    EXPECT_NEAR(q.y, 2.5f, EPSILON);
    EXPECT_NEAR(q.z, 3.5f, EPSILON);
    EXPECT_NEAR(q.w, 4.5f, EPSILON);

    Vec3<float>       vec(1.0f, 2.0f, 3.0f);
    Quaternion<float> vq(vec, 4.0f);
    EXPECT_NEAR(vq.x, 1.0f, EPSILON);
    EXPECT_NEAR(vq.y, 2.0f, EPSILON);
    EXPECT_NEAR(vq.z, 3.0f, EPSILON);
    EXPECT_NEAR(vq.w, 4.0f, EPSILON);
}

TEST(QuaternionTest, CopyConstruction)
{
    Quaternion<float> original(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> copy(original);

    EXPECT_NEAR(copy.x, original.x, EPSILON);
    EXPECT_NEAR(copy.y, original.y, EPSILON);
    EXPECT_NEAR(copy.z, original.z, EPSILON);
    EXPECT_NEAR(copy.w, original.w, EPSILON);
}

// ========== ARITHMETIC TESTS ==========

TEST(QuaternionTest, Addition)
{
    Quaternion<float> q1(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> q2(5.0f, 6.0f, 7.0f, 8.0f);
    Quaternion<float> result = q1 + q2;

    EXPECT_NEAR(result.x, 6.0f, EPSILON);
    EXPECT_NEAR(result.y, 8.0f, EPSILON);
    EXPECT_NEAR(result.z, 10.0f, EPSILON);
    EXPECT_NEAR(result.w, 12.0f, EPSILON);

    Quaternion<float> q3(1.0f, 2.0f, 3.0f, 4.0f);
    q3 += q2;
    EXPECT_NEAR(q3.x, 6.0f, EPSILON);
    EXPECT_NEAR(q3.w, 12.0f, EPSILON);
}

TEST(QuaternionTest, Subtraction)
{
    Quaternion<float> q1(5.0f, 6.0f, 7.0f, 8.0f);
    Quaternion<float> q2(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> result = q1 - q2;

    EXPECT_NEAR(result.x, 4.0f, EPSILON);
    EXPECT_NEAR(result.y, 4.0f, EPSILON);
    EXPECT_NEAR(result.z, 4.0f, EPSILON);
    EXPECT_NEAR(result.w, 4.0f, EPSILON);

    Quaternion<float> q3(5.0f, 6.0f, 7.0f, 8.0f);
    q3 -= q2;
    EXPECT_NEAR(q3.x, 4.0f, EPSILON);
    EXPECT_NEAR(q3.w, 4.0f, EPSILON);
}

TEST(QuaternionTest, ScalarMultiplication)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> result = q * 2.0f;

    EXPECT_NEAR(result.x, 2.0f, EPSILON);
    EXPECT_NEAR(result.y, 4.0f, EPSILON);
    EXPECT_NEAR(result.z, 6.0f, EPSILON);
    EXPECT_NEAR(result.w, 8.0f, EPSILON);

    Quaternion<float> q2(1.0f, 2.0f, 3.0f, 4.0f);
    q2 *= 3.0f;
    EXPECT_NEAR(q2.x, 3.0f, EPSILON);
    EXPECT_NEAR(q2.w, 12.0f, EPSILON);
}

TEST(QuaternionTest, ScalarDivision)
{
    Quaternion<float> q(2.0f, 4.0f, 6.0f, 8.0f);
    Quaternion<float> result = q / 2.0f;

    EXPECT_NEAR(result.x, 1.0f, EPSILON);
    EXPECT_NEAR(result.y, 2.0f, EPSILON);
    EXPECT_NEAR(result.z, 3.0f, EPSILON);
    EXPECT_NEAR(result.w, 4.0f, EPSILON);

    Quaternion<float> q2(4.0f, 8.0f, 12.0f, 16.0f);
    q2 /= 4.0f;
    EXPECT_NEAR(q2.x, 1.0f, EPSILON);
    EXPECT_NEAR(q2.w, 4.0f, EPSILON);
}

TEST(QuaternionTest, Negation)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> result = -q;

    EXPECT_NEAR(result.x, -1.0f, EPSILON);
    EXPECT_NEAR(result.y, -2.0f, EPSILON);
    EXPECT_NEAR(result.z, -3.0f, EPSILON);
    EXPECT_NEAR(result.w, -4.0f, EPSILON);
}

TEST(QuaternionTest, QuaternionMultiplication)
{
    Quaternion<float> q1(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> q2(5.0f, 6.0f, 7.0f, 8.0f);
    Quaternion<float> result = q1 * q2;

    EXPECT_NEAR(result.x, 24.0f, EPSILON);
    EXPECT_NEAR(result.y, 48.0f, EPSILON);
    EXPECT_NEAR(result.z, 48.0f, EPSILON);
    EXPECT_NEAR(result.w, -6.0f, EPSILON);

    Quaternion<float> q3(1.0f, 2.0f, 3.0f, 4.0f);
    q3 *= q2;
    EXPECT_NEAR(q3.x, 24.0f, EPSILON);
    EXPECT_NEAR(q3.w, -6.0f, EPSILON);
}

TEST(QuaternionTest, IdentityMultiplication)
{
    Quaternion<float> identity(0.0f, 0.0f, 0.0f, 1.0f);
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);

    Quaternion<float> result1 = identity * q;
    Quaternion<float> result2 = q * identity;

    EXPECT_NEAR(result1.x, q.x, EPSILON);
    EXPECT_NEAR(result1.y, q.y, EPSILON);
    EXPECT_NEAR(result1.z, q.z, EPSILON);
    EXPECT_NEAR(result1.w, q.w, EPSILON);

    EXPECT_NEAR(result2.x, q.x, EPSILON);
    EXPECT_NEAR(result2.y, q.y, EPSILON);
    EXPECT_NEAR(result2.z, q.z, EPSILON);
    EXPECT_NEAR(result2.w, q.w, EPSILON);
}

TEST(QuaternionTest, QuaternionDivision)
{
    Quaternion<float> q1(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> q2(2.0f, 0.0f, 0.0f, 2.0f);
    Quaternion<float> result = q1 / q2;
    Quaternion<float> check  = result * q2;

    EXPECT_NEAR(check.x / q1.x, 1.0f, EPSILON);
    EXPECT_NEAR(check.y / q1.y, 1.0f, EPSILON);
    EXPECT_NEAR(check.z / q1.z, 1.0f, EPSILON);
    EXPECT_NEAR(check.w / q1.w, 1.0f, EPSILON);
}

// ========== DOT PRODUCT TESTS ==========

TEST(QuaternionTest, DotProduct)
{
    Quaternion<float> q1(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> q2(5.0f, 6.0f, 7.0f, 8.0f);
    float             result   = q1.dot(q2);
    float             expected = 70.0f;
    EXPECT_NEAR(result, expected, EPSILON);
}

TEST(QuaternionTest, DotProductWithSelf)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    float             result   = q.dot(q);
    float             expected = 30.0f;
    EXPECT_NEAR(result, expected, EPSILON);
}

// ========== NORMALIZATION TESTS ==========

TEST(QuaternionTest, Normalize)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> normalized = q.normalize();
    float             length     = sqrt(normalized.dot(normalized));
    EXPECT_NEAR(length, 1.0f, EPSILON);
}

TEST(QuaternionTest, NormalizeIdentity)
{
    Quaternion<float> identity(0.0f, 0.0f, 0.0f, 1.0f);
    Quaternion<float> normalized = identity.normalize();
    EXPECT_NEAR(normalized.x, 0.0f, EPSILON);
    EXPECT_NEAR(normalized.y, 0.0f, EPSILON);
    EXPECT_NEAR(normalized.z, 0.0f, EPSILON);
    EXPECT_NEAR(normalized.w, 1.0f, EPSILON);
}

// ========== CONJUGATE AND INVERSE TESTS ==========

TEST(QuaternionTest, Conjugate)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> conj = q.conjugate();

    EXPECT_NEAR(conj.x, -1.0f, EPSILON);
    EXPECT_NEAR(conj.y, -2.0f, EPSILON);
    EXPECT_NEAR(conj.z, -3.0f, EPSILON);
    EXPECT_NEAR(conj.w, 4.0f, EPSILON);
}

TEST(QuaternionTest, ConjugateOfConjugate)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> doubleConj = q.conjugate().conjugate();

    EXPECT_NEAR(doubleConj.x, q.x, EPSILON);
    EXPECT_NEAR(doubleConj.y, q.y, EPSILON);
    EXPECT_NEAR(doubleConj.z, q.z, EPSILON);
    EXPECT_NEAR(doubleConj.w, q.w, EPSILON);
}

TEST(QuaternionTest, Inverse)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    q                         = q.normalize();

    Quaternion<float> inv     = q.inverse();
    Quaternion<float> product = q * inv;

    EXPECT_NEAR(product.x, 0.0f, EPSILON);
    EXPECT_NEAR(product.y, 0.0f, EPSILON);
    EXPECT_NEAR(product.z, 0.0f, EPSILON);
    EXPECT_NEAR(product.w, 1.0f, EPSILON);
}

TEST(QuaternionTest, InverseOfIdentity)
{
    Quaternion<float> identity(0.0f, 0.0f, 0.0f, 1.0f);
    Quaternion<float> inv = identity.inverse();

    EXPECT_NEAR(inv.x, 0.0f, EPSILON);
    EXPECT_NEAR(inv.y, 0.0f, EPSILON);
    EXPECT_NEAR(inv.z, 0.0f, EPSILON);
    EXPECT_NEAR(inv.w, 1.0f, EPSILON);
}

// ========== CONVERSION TESTS ==========

TEST(QuaternionTest, FromEulerAngles)
{
    float             pitch  = PI<float> / 4;
    float             yaw    = PI<float> / 6;
    float             roll   = PI<float> / 3;

    Quaternion<float> q      = fromEulerAngles(pitch, yaw, roll);
    float             length = sqrt(q.dot(q));
    EXPECT_NEAR(length, 1.0f, EPSILON);
}

TEST(QuaternionTest, FromEulerAnglesVector)
{
    Vec3<float>       rotation(PI<float> / 4, PI<float> / 6, PI<float> / 3);
    Quaternion<float> q      = fromEulerAngles(rotation);

    float             length = sqrt(q.dot(q));
    EXPECT_NEAR(length, 1.0f, EPSILON);
}

TEST(QuaternionTest, FromAxisAngle)
{
    Vec3<float>       axis(0.0f, 0.0f, 1.0f);
    float             angle      = PI<float> / 2;

    Quaternion<float> q          = fromAxisAngle(axis, angle);
    float             expected_z = sin(PI<float> / 4);
    float             expected_w = cos(PI<float> / 4);

    EXPECT_NEAR(q.x, 0.0f, EPSILON);
    EXPECT_NEAR(q.y, 0.0f, EPSILON);
    EXPECT_NEAR(q.z, expected_z, EPSILON);
    EXPECT_NEAR(q.w, expected_w, EPSILON);
}

TEST(QuaternionTest, FromRotationVector)
{
    Vec3<float>       zeroRotation(0.0f, 0.0f, 0.0f);
    Quaternion<float> q = fromRotationVector(zeroRotation);

    EXPECT_NEAR(q.x, 0.0f, EPSILON);
    EXPECT_NEAR(q.y, 0.0f, EPSILON);
    EXPECT_NEAR(q.z, 0.0f, EPSILON);
    EXPECT_NEAR(q.w, 1.0f, EPSILON);
}

TEST(QuaternionTest, ToRotationVector)
{
    Vec3<float>       originalRotation(PI<float> / 4, 0.0f, 0.0f);
    Quaternion<float> q      = fromRotationVector(originalRotation);
    Vec3<float>       result = toRotationVector(q);

    EXPECT_NEAR(result.x, originalRotation.x, EPSILON);
    EXPECT_NEAR(result.y, originalRotation.y, EPSILON);
    EXPECT_NEAR(result.z, originalRotation.z, EPSILON);
}

// ========== INTERPOLATION TESTS ==========

TEST(QuaternionTest, LerpBoundaryConditions)
{
    Quaternion<float> q1(1.0f, 0.0f, 0.0f, 0.0f);
    Quaternion<float> q2(0.0f, 1.0f, 0.0f, 0.0f);
    q1                        = q1.normalize();
    q2                        = q2.normalize();

    Quaternion<float> result1 = lerp(q1, q2, 0.0f);
    Quaternion<float> result2 = lerp(q1, q2, 1.0f);

    EXPECT_NEAR(result1.x, q1.normalize().x, EPSILON);
    EXPECT_NEAR(result1.w, q1.normalize().w, EPSILON);
    EXPECT_NEAR(result2.x, q2.normalize().x, EPSILON);
    EXPECT_NEAR(result2.w, q2.normalize().w, EPSILON);
}

TEST(QuaternionTest, SlerpBoundaryConditions)
{
    Quaternion<float> q1(1.0f, 0.0f, 0.0f, 0.0f);
    Quaternion<float> q2(0.0f, 1.0f, 0.0f, 0.0f);
    q1                        = q1.normalize();
    q2                        = q2.normalize();

    Quaternion<float> result1 = slerp(q1, q2, 0.0f);
    Quaternion<float> result2 = slerp(q1, q2, 1.0f);

    EXPECT_NEAR(result1.x, q1.normalize().x, EPSILON);
    EXPECT_NEAR(result1.w, q1.normalize().w, EPSILON);
    EXPECT_NEAR(result2.x, q2.normalize().x, EPSILON);
    EXPECT_NEAR(result2.w, q2.normalize().w, EPSILON);
}

// ========== EDGE CASES AND ERROR CONDITIONS ==========

TEST(QuaternionTest, ZeroDivision)
{
    Quaternion<float> q(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion<float> result = q / 0.0f;

    EXPECT_TRUE(std::isinf(result.x) || std::abs(result.x) > 1e30f);
    EXPECT_TRUE(std::isinf(result.y) || std::abs(result.y) > 1e30f);
    EXPECT_TRUE(std::isinf(result.z) || std::abs(result.z) > 1e30f);
    EXPECT_TRUE(std::isinf(result.w) || std::abs(result.w) > 1e30f);
}

TEST(QuaternionTest, LargeValueHandling)
{
    Quaternion<float> q(1e10f, 1e10f, 1e10f, 1e10f);
    Quaternion<float> result = q * 2.0f;

    EXPECT_NEAR(result.x, 2e10f, 1e5f);
    EXPECT_NEAR(result.w, 2e10f, 1e5f);
}

TEST(QuaternionTest, ChainedOperations)
{
    Quaternion<float> identity(0.0f, 0.0f, 0.0f, 1.0f);
    Quaternion<float> q(0.1f, 0.2f, 0.3f, 1.0f);
    q                        = q.normalize();

    Quaternion<float> result = identity;

    for (int i = 0; i < 100; ++i)
    {
        result = result * q;
        result = result.normalize();
    }

    float length = sqrt(result.dot(result));
    EXPECT_NEAR(length, 1.0f, EPSILON);
}

TEST(QuaternionTest, QuaternionToMat4)
{
    Quaternion<float> identity(0.0f, 0.0f, 0.0f, 1.0f);
    Mat4f             result           = quaternionToMat4(identity);

    Mat4f             expectedIdentity = Identity<Mat4f>();
    for (size_t i = 0; i < 4; ++i)
    {
        for (size_t j = 0; j < 4; ++j)
        {
            EXPECT_NEAR(result(i, j), expectedIdentity(i, j), EPSILON);
        }
    }

    float             angle = radians(90.0f);
    Quaternion<float> rotZ(0.0f, 0.0f, sin(angle * 0.5f), cos(angle * 0.5f));
    Mat4f             rotMatrix = quaternionToMat4(rotZ);

    EXPECT_NEAR(rotMatrix(0, 0), 0.0f, EPSILON);
    EXPECT_NEAR(rotMatrix(0, 1), -1.0f, EPSILON);
    EXPECT_NEAR(rotMatrix(1, 0), 1.0f, EPSILON);
    EXPECT_NEAR(rotMatrix(1, 1), 0.0f, EPSILON);
    EXPECT_NEAR(rotMatrix(2, 2), 1.0f, EPSILON);
    EXPECT_NEAR(rotMatrix(3, 3), 1.0f, EPSILON);

    Quaternion<float> arbitrary(0.1f, 0.2f, 0.3f, 0.4f);
    arbitrary       = arbitrary.normalize();
    Mat4f arbMatrix = quaternionToMat4(arbitrary);

    EXPECT_NEAR(arbMatrix(0, 3), 0.0f, EPSILON);
    EXPECT_NEAR(arbMatrix(1, 3), 0.0f, EPSILON);
    EXPECT_NEAR(arbMatrix(2, 3), 0.0f, EPSILON);
    EXPECT_NEAR(arbMatrix(3, 0), 0.0f, EPSILON);
    EXPECT_NEAR(arbMatrix(3, 1), 0.0f, EPSILON);
    EXPECT_NEAR(arbMatrix(3, 2), 0.0f, EPSILON);
    EXPECT_NEAR(arbMatrix(3, 3), 1.0f, EPSILON);
}

TEST(QuaternionTest, QuaternionRotate)
{
    Quaternion<float> identity(0.0f, 0.0f, 0.0f, 1.0f);
    Vec3f             vector(1.0f, 2.0f, 3.0f);
    Vec3f             result = rotate(identity, vector);

    EXPECT_NEAR(result.x, vector.x, EPSILON);
    EXPECT_NEAR(result.y, vector.y, EPSILON);
    EXPECT_NEAR(result.z, vector.z, EPSILON);

    float             angle = radians(90.0f);
    Quaternion<float> rotZ(0.0f, 0.0f, sin(angle * 0.5f), cos(angle * 0.5f));
    Vec3f             xAxis(1.0f, 0.0f, 0.0f);
    Vec3f             rotatedX = rotate(rotZ, xAxis);

    EXPECT_NEAR(rotatedX.x, 0.0f, EPSILON);
    EXPECT_NEAR(rotatedX.y, 1.0f, EPSILON);
    EXPECT_NEAR(rotatedX.z, 0.0f, EPSILON);

    angle = radians(180.0f);
    Quaternion<float> rot180Y(0.0f, sin(angle * 0.5f), 0.0f, cos(angle * 0.5f));
    rot180Y = rot180Y.normalize();
    Vec3f zAxis(0.0f, 0.0f, 1.0f);
    Vec3f rotatedZ = rotate(rot180Y, zAxis);

    EXPECT_NEAR(rotatedZ.x, 0.0f, EPSILON);
    EXPECT_NEAR(rotatedZ.y, 0.0f, EPSILON);
    EXPECT_NEAR(rotatedZ.z, -1.0f, EPSILON);

    Quaternion<float> arbitrary(0.1f, 0.2f, 0.3f, 0.4f);
    arbitrary = arbitrary.normalize();
    Vec3f originalVec(3.0f, 4.0f, 5.0f);
    Vec3f rotatedVec        = rotate(arbitrary, originalVec);

    float originalMagnitude = originalVec.magnitude();
    float rotatedMagnitude  = rotatedVec.magnitude();
    EXPECT_NEAR(originalMagnitude, rotatedMagnitude, EPSILON);
}