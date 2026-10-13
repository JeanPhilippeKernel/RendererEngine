#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cmath>

namespace ZEngine::Core::Maths
{
    template <typename T, size_t R, size_t C, typename = std::enable_if_t<std::is_arithmetic_v<T> && (R >= 1) && (C >= 1)>>
    struct Matrix
    {
        T  m_data[C][R]; // column-major storage

        T& operator()(size_t row, size_t col)
        {
            ZENGINE_VALIDATE_ASSERT(row < R && col < C, "Index out of range");
            return m_data[col][row];
        }

        const T& operator()(size_t row, size_t col) const
        {
            ZENGINE_VALIDATE_ASSERT(row < R && col < C, "Index out of range");
            return m_data[col][row];
        }

        Matrix<T, R, C> operator+(const Matrix<T, R, C>& other) const
        {
            Matrix<T, R, C> result{};
            for (size_t j = 0; j < C; ++j)
            {
                for (size_t i = 0; i < R; i++)
                {
                    result(i, j) = (*this)(i, j) + other(i, j);
                }
            }
            return result;
        }

        Matrix<T, R, C> operator-(const Matrix<T, R, C>& other) const
        {
            Matrix<T, R, C> result{};
            for (size_t j = 0; j < C; ++j)
            {
                for (size_t i = 0; i < R; i++)
                {
                    result(i, j) = (*this)(i, j) - other(i, j);
                }
            }
            return result;
        }

        Matrix<T, R, C> operator*(T scalar) const
        {
            Matrix<T, R, C> result{};
            for (size_t j = 0; j < C; ++j)
            {
                for (size_t i = 0; i < R; ++i)
                {
                    result(i, j) = (*this)(i, j) * scalar;
                }
            }

            return result;
        }

        Matrix<T, R, C>& operator+=(const Matrix<T, R, C>& other)
        {
            for (size_t j = 0; j < C; ++j)
            {
                for (size_t i = 0; i < R; ++i)
                {
                    (*this)(i, j) += other(i, j);
                }
            }
            return *this;
        }

        Matrix<T, R, C>& operator-=(const Matrix<T, R, C>& other)
        {
            for (size_t j = 0; j < C; ++j)
            {
                for (size_t i = 0; i < R; ++i)
                {
                    (*this)(i, j) -= other(i, j);
                }
            }
            return *this;
        }

        Matrix<T, R, C>& operator*=(T scalar)
        {
            for (unsigned j = 0; j < C; ++j)
            {
                for (unsigned i = 0; i < R; ++i)
                {
                    (*this)(i, j) *= scalar;
                }
            }
            return *this;
        }

        Matrix<T, R, C> operator/(T scalar) const
        {
            Matrix<T, R, C> result = (*this);
            scalar                 = T(1) / scalar;
            for (unsigned j = 0; j < C; ++j)
            {
                for (unsigned i = 0; i < R; ++i)
                {
                    result(i, j) *= scalar;
                }
            }
            return result;
        }

        Matrix<T, R, C>& operator/=(T scalar)
        {
            scalar = T(1) / scalar;
            for (unsigned j = 0; j < C; ++j)
            {
                for (unsigned i = 0; i < R; ++i)
                {
                    (*this)(i, j) *= scalar;
                }
            }
            return *this;
        }
    };

    template <typename T>
    struct Mat2 : public Matrix<T, 2, 2>
    {
        using Matrix<T, 2, 2>::m_data;

        Mat2()
        {
            Helpers::secure_memset(m_data, 0, sizeof(m_data), sizeof(m_data));
        }

        Mat2(T m00, T m01, T m10, T m11)
        {
            m_data[0][0] = m00;
            m_data[0][1] = m10;
            m_data[1][0] = m01;
            m_data[1][1] = m11;
        }

        Mat2(const Vec2<T>& a, const Vec2<T>& b)
        {
            m_data[0][0] = a.x;
            m_data[0][1] = a.y;
            m_data[1][0] = b.x;
            m_data[1][1] = b.y;
        }

        Mat2(const Matrix<T, 2, 2>& other)
        {
            for (unsigned j = 0; j < 2; ++j)
            {
                for (unsigned i = 0; i < 2; ++i)
                {
                    m_data[j][i] = other.m_data[j][i];
                }
            }
        }

        Mat2& operator+=(const Mat2& other)
        {
            Matrix<T, 2, 2>::operator+=(other);
            return *this;
        }

        Mat2& operator-=(const Mat2& other)
        {
            Matrix<T, 2, 2>::operator-=(other);
            return *this;
        }

        Mat2 Inverse() const
        {
            T det = Determinant(*this);
            ZENGINE_VALIDATE_ASSERT(det != T(0), "Matrix is singular and cannot be inverted");

            T invDet = T(1) / det;

            return Mat2<T>(m_data[1][1] * invDet, -m_data[0][1] * invDet, -m_data[1][0] * invDet, m_data[0][0] * invDet);
        }
    };

    template <typename T>
    struct Mat3 : public Matrix<T, 3, 3>
    {
        using Matrix<T, 3, 3>::m_data;

        Mat3()
        {
            Helpers::secure_memset(m_data, 0, sizeof(m_data), sizeof(m_data));
        }

        Mat3(T m00, T m01, T m02, T m10, T m11, T m12, T m20, T m21, T m22)
        {
            m_data[0][0] = m00;
            m_data[0][1] = m10;
            m_data[0][2] = m20;

            m_data[1][0] = m01;
            m_data[1][1] = m11;
            m_data[1][2] = m21;

            m_data[2][0] = m02;
            m_data[2][1] = m12;
            m_data[2][2] = m22;
        }

        Mat3(const Vec3<T>& a, const Vec3<T>& b, const Vec3<T>& c)
        {
            m_data[0][0] = a.x;
            m_data[0][1] = a.y;
            m_data[0][2] = a.z;
            m_data[1][0] = b.x;
            m_data[1][1] = b.y;
            m_data[1][2] = b.z;
            m_data[2][0] = c.x;
            m_data[2][1] = c.y;
            m_data[2][2] = c.z;
        }

        Mat3(const Matrix<T, 3, 3>& other)
        {
            for (unsigned j = 0; j < 3; ++j)
            {
                for (unsigned i = 0; i < 3; ++i)
                {
                    m_data[j][i] = other.m_data[j][i];
                }
            }
        }

        Mat3& operator+=(const Mat3& other)
        {
            Matrix<T, 3, 3>::operator+=(other);
            return *this;
        }

        Mat3& operator-=(const Mat3& other)
        {
            Matrix<T, 3, 3>::operator-=(other);
            return *this;
        }

        Vec3<T>& operator[](int j)
        {
            return (*reinterpret_cast<Vec3<T>*>(m_data[j]));
        }

        const Vec3<T>& operator[](int j) const
        {
            return (*reinterpret_cast<const Vec3<T>*>(m_data[j]));
        }

        Mat3 Inverse() const
        {
            auto&          M      = *this;

            const Vec3<T>& a      = M[0];
            const Vec3<T>& b      = M[1];
            const Vec3<T>& c      = M[2];

            Vec3<T>        r0     = cross3d(b, c);
            Vec3<T>        r1     = cross3d(c, a);
            Vec3<T>        r2     = cross3d(a, b);

            float          invDet = 1.0f / dot(r2, c);

            return Mat3<T>(r0.x * invDet, r0.y * invDet, r0.z * invDet, r1.x * invDet, r1.y * invDet, r1.z * invDet, r2.x * invDet, r2.y * invDet, r2.z * invDet);
        };
    };

    template <typename T>
    struct Mat4 : public Matrix<T, 4, 4>
    {
        using Matrix<T, 4, 4>::m_data;
        Mat4()
        {
            Helpers::secure_memset(m_data, 0, sizeof(m_data), sizeof(m_data));
        }

        Mat4(T m00, T m01, T m02, T m03, T m10, T m11, T m12, T m13, T m20, T m21, T m22, T m23, T m30, T m31, T m32, T m33)
        {
            m_data[0][0] = m00;
            m_data[1][0] = m01;
            m_data[2][0] = m02;
            m_data[3][0] = m03;

            m_data[0][1] = m10;
            m_data[1][1] = m11;
            m_data[2][1] = m12;
            m_data[3][1] = m13;

            m_data[0][2] = m20;
            m_data[1][2] = m21;
            m_data[2][2] = m22;
            m_data[3][2] = m23;

            m_data[0][3] = m30;
            m_data[1][3] = m31;
            m_data[2][3] = m32;
            m_data[3][3] = m33;
        }

        Mat4(const Matrix<T, 4, 4>& other)
        {
            for (unsigned j = 0; j < 4; ++j)
            {
                for (unsigned i = 0; i < 4; ++i)
                {
                    m_data[j][i] = other.m_data[j][i];
                }
            }
        }

        Mat4& operator+=(const Mat4& other)
        {
            Matrix<T, 4, 4>::operator+=(other);
            return *this;
        }

        Mat4& operator-=(const Mat4& other)
        {
            Matrix<T, 4, 4>::operator-=(other);
            return *this;
        }

        Mat4<T> Inverse() const
        {
            Mat4<T> working = *this;
            Mat4<T> inverse = {};
            for (size_t index = 0; index < 4; ++index)
                inverse(index, index) = T(1);

            // Partial pivoting makes this valid for both affine transforms and
            // projective matrices such as a camera view-projection transform.
            for (size_t column = 0; column < 4; ++column)
            {
                size_t pivot_row       = column;
                T      pivot_magnitude = working(column, column) < T(0) ? -working(column, column) : working(column, column);
                for (size_t row = column + 1; row < 4; ++row)
                {
                    const T candidate           = working(row, column);
                    const T candidate_magnitude = candidate < T(0) ? -candidate : candidate;
                    if (candidate_magnitude > pivot_magnitude)
                    {
                        pivot_magnitude = candidate_magnitude;
                        pivot_row       = row;
                    }
                }

                ZENGINE_VALIDATE_ASSERT(pivot_magnitude != T(0), "Matrix is singular and cannot be inverted");
                if (pivot_magnitude == T(0))
                    return inverse;

                if (pivot_row != column)
                {
                    for (size_t index = 0; index < 4; ++index)
                    {
                        const T working_value     = working(column, index);
                        working(column, index)    = working(pivot_row, index);
                        working(pivot_row, index) = working_value;

                        const T inverse_value     = inverse(column, index);
                        inverse(column, index)    = inverse(pivot_row, index);
                        inverse(pivot_row, index) = inverse_value;
                    }
                }

                const T reciprocal_pivot = T(1) / working(column, column);
                for (size_t index = 0; index < 4; ++index)
                {
                    working(column, index) *= reciprocal_pivot;
                    inverse(column, index) *= reciprocal_pivot;
                }

                for (size_t row = 0; row < 4; ++row)
                {
                    if (row == column)
                        continue;

                    const T factor = working(row, column);
                    if (factor == T(0))
                        continue;

                    for (size_t index = 0; index < 4; ++index)
                    {
                        working(row, index) -= factor * working(column, index);
                        inverse(row, index) -= factor * inverse(column, index);
                    }
                }
            }

            return inverse;
        }
        Vec4<T>& operator[](int j)
        {
            return (*reinterpret_cast<Vec4<T>*>(m_data[j]));
        }

        const Vec4<T>& operator[](int j) const
        {
            return (*reinterpret_cast<const Vec4<T>*>(m_data[j]));
        }
    };

    using IMat2 = Mat2<int>;
    using Mat2f = Mat2<float>;
    using Mat3f = Mat3<float>;
    using Mat4f = Mat4<float>;

    template <typename T>
    T Identity();

    template <>
    inline Mat2f Identity<Mat2f>()
    {
        return Mat2f(1, 0, 0, 1);
    }

    template <>
    inline Mat3f Identity<Mat3f>()
    {
        return Mat3f(1, 0, 0, 0, 1, 0, 0, 0, 1);
    }

    template <>
    inline Mat4f Identity<Mat4f>()
    {
        return Mat4f(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    }

    template <typename T>
    inline Mat3<T> operator*(const Mat3<T>& a, const Mat3<T>& b)
    {
        return (Mat3<T>(
            a(0, 0) * b(0, 0) + a(0, 1) * b(1, 0) + a(0, 2) * b(2, 0),
            a(0, 0) * b(0, 1) + a(0, 1) * b(1, 1) + a(0, 2) * b(2, 1),
            a(0, 0) * b(0, 2) + a(0, 1) * b(1, 2) + a(0, 2) * b(2, 2),

            a(1, 0) * b(0, 0) + a(1, 1) * b(1, 0) + a(1, 2) * b(2, 0),
            a(1, 0) * b(0, 1) + a(1, 1) * b(1, 1) + a(1, 2) * b(2, 1),
            a(1, 0) * b(0, 2) + a(1, 1) * b(1, 2) + a(1, 2) * b(2, 2),

            a(2, 0) * b(0, 0) + a(2, 1) * b(1, 0) + a(2, 2) * b(2, 0),
            a(2, 0) * b(0, 1) + a(2, 1) * b(1, 1) + a(2, 2) * b(2, 1),
            a(2, 0) * b(0, 2) + a(2, 1) * b(1, 2) + a(2, 2) * b(2, 2)));
    }

    template <typename T>
    inline Vec3<T> operator*(const Mat3<T>& a, const Vec3<T>& v)
    {
        return Vec3<T>(a(0, 0) * v.x + a(0, 1) * v.y + a(0, 2) * v.z, a(1, 0) * v.x + a(1, 1) * v.y + a(1, 2) * v.z, a(2, 0) * v.x + a(2, 1) * v.y + a(2, 2) * v.z);
    }

    template <typename T>
    inline Vec4<T> operator*(const Mat4<T>& a, const Vec4<T>& vec)
    {
        return Vec4<T>(a(0, 0) * vec.x + a(0, 1) * vec.y + a(0, 2) * vec.z + a(0, 3) * vec.w, a(1, 0) * vec.x + a(1, 1) * vec.y + a(1, 2) * vec.z + a(1, 3) * vec.w, a(2, 0) * vec.x + a(2, 1) * vec.y + a(2, 2) * vec.z + a(2, 3) * vec.w, a(3, 0) * vec.x + a(3, 1) * vec.y + a(3, 2) * vec.z + a(3, 3) * vec.w);
    }

    template <typename T>
    T Determinant(const Mat2<T>& M)
    {
        return (M(0, 0) * M(1, 1)) - (M(1, 0) * M(0, 1));
    }

    template <typename T>
    inline T Determinant(const Mat3<T>& M)
    {
        return (M(0, 0) * (M(1, 1) * M(2, 2) - M(1, 2) * M(2, 1)) - M(0, 1) * (M(1, 0) * M(2, 2) - M(1, 2) * M(2, 0)) + M(0, 2) * (M(1, 0) * M(2, 1) - M(1, 1) * M(2, 0)));
    }

    template <typename T>
    inline T Determinant(const Mat4<T>& M)
    {
        T m00 = M(0, 0), m01 = M(0, 1), m02 = M(0, 2), m03 = M(0, 3);
        T m10 = M(1, 0), m11 = M(1, 1), m12 = M(1, 2), m13 = M(1, 3);
        T m20 = M(2, 0), m21 = M(2, 1), m22 = M(2, 2), m23 = M(2, 3);
        T m30 = M(3, 0), m31 = M(3, 1), m32 = M(3, 2), m33 = M(3, 3);

        return m00 * (m11 * (m22 * m33 - m23 * m32) - m12 * (m21 * m33 - m23 * m31) + m13 * (m21 * m32 - m22 * m31)) - m01 * (m10 * (m22 * m33 - m23 * m32) - m12 * (m20 * m33 - m23 * m30) + m13 * (m20 * m32 - m22 * m30)) + m02 * (m10 * (m21 * m33 - m23 * m31) - m11 * (m20 * m33 - m23 * m30) + m13 * (m20 * m31 - m21 * m30)) - m03 * (m10 * (m21 * m32 - m22 * m31) - m11 * (m20 * m32 - m22 * m30) + m12 * (m20 * m31 - m21 * m30));
    }

    template <typename T>
    inline Mat4<T> operator*(const Mat4<T>& a, const Mat4<T>& b)
    {
        return (Mat4<T>(
            a(0, 0) * b(0, 0) + a(0, 1) * b(1, 0) + a(0, 2) * b(2, 0) + a(0, 3) * b(3, 0),
            a(0, 0) * b(0, 1) + a(0, 1) * b(1, 1) + a(0, 2) * b(2, 1) + a(0, 3) * b(3, 1),
            a(0, 0) * b(0, 2) + a(0, 1) * b(1, 2) + a(0, 2) * b(2, 2) + a(0, 3) * b(3, 2),
            a(0, 0) * b(0, 3) + a(0, 1) * b(1, 3) + a(0, 2) * b(2, 3) + a(0, 3) * b(3, 3),

            a(1, 0) * b(0, 0) + a(1, 1) * b(1, 0) + a(1, 2) * b(2, 0) + a(1, 3) * b(3, 0),
            a(1, 0) * b(0, 1) + a(1, 1) * b(1, 1) + a(1, 2) * b(2, 1) + a(1, 3) * b(3, 1),
            a(1, 0) * b(0, 2) + a(1, 1) * b(1, 2) + a(1, 2) * b(2, 2) + a(1, 3) * b(3, 2),
            a(1, 0) * b(0, 3) + a(1, 1) * b(1, 3) + a(1, 2) * b(2, 3) + a(1, 3) * b(3, 3),

            a(2, 0) * b(0, 0) + a(2, 1) * b(1, 0) + a(2, 2) * b(2, 0) + a(2, 3) * b(3, 0),
            a(2, 0) * b(0, 1) + a(2, 1) * b(1, 1) + a(2, 2) * b(2, 1) + a(2, 3) * b(3, 1),
            a(2, 0) * b(0, 2) + a(2, 1) * b(1, 2) + a(2, 2) * b(2, 2) + a(2, 3) * b(3, 2),
            a(2, 0) * b(0, 3) + a(2, 1) * b(1, 3) + a(2, 2) * b(2, 3) + a(2, 3) * b(3, 3),

            a(3, 0) * b(0, 0) + a(3, 1) * b(1, 0) + a(3, 2) * b(2, 0) + a(3, 3) * b(3, 0),
            a(3, 0) * b(0, 1) + a(3, 1) * b(1, 1) + a(3, 2) * b(2, 1) + a(3, 3) * b(3, 1),
            a(3, 0) * b(0, 2) + a(3, 1) * b(1, 2) + a(3, 2) * b(2, 2) + a(3, 3) * b(3, 2),
            a(3, 0) * b(0, 3) + a(3, 1) * b(1, 3) + a(3, 2) * b(2, 3) + a(3, 3) * b(3, 3)));
    }

    // Declared here because strict decomposition verifies its result by round-tripping
    // through the canonical local-transform composer defined below.
    inline Mat4f ComposeTransformMatrix(const Vec3f& position, const Vec3f& rotation, const Vec3f& scale);

    /// @brief Tolerances used when converting an affine matrix into authored local TRS.
    /// @details TransformComponent deliberately has no shear or general-affine representation.
    ///          Editor tools must therefore reject matrices which cannot round-trip through
    ///          ComposeTransformMatrix rather than approximating them and corrupting a scene.
    struct TransformTRSDecompositionTolerance
    {
        float MinimumScale           = 1.0e-5f;
        float OrthogonalityTolerance = 1.0e-4f;
        float RecompositionTolerance = 1.0e-4f;
    };

    /// @brief Strictly decomposes an affine matrix into the engine's authored YXZ Euler TRS.
    /// @returns false when @p transform is non-finite, projective, singular, sheared, or would
    ///          fail a ComposeTransformMatrix round trip. Output arguments are untouched on
    ///          failure.
    /// @details A reflection has several mathematically equivalent TRS encodings. The engine
    ///          canonicalizes it by carrying a negative determinant on Scale.x; Scale.y and
    ///          Scale.z are non-negative. This gives history, serialization, and gizmo previews
    ///          stable authored values without changing the represented matrix.
    inline bool TryDecomposeTransformTRS(const Mat4f& transform, Vec3f& translation, Vec3f& rotation, Vec3f& scale, const TransformTRSDecompositionTolerance& tolerance = {})
    {
        const auto finite = [](float value) { return std::isfinite(value); };
        for (size_t row = 0; row < 4; ++row)
        {
            for (size_t column = 0; column < 4; ++column)
            {
                if (!finite(transform(row, column)))
                    return false;
            }
        }

        if (!finite(tolerance.MinimumScale) || !finite(tolerance.OrthogonalityTolerance) || !finite(tolerance.RecompositionTolerance) || tolerance.MinimumScale <= 0.f || tolerance.OrthogonalityTolerance < 0.f || tolerance.RecompositionTolerance < 0.f)
            return false;

        // TransformComponent accepts affine matrices only. Translation lives in the final
        // column because Matrix is column-major.
        if (std::fabs(transform(3, 0)) > tolerance.RecompositionTolerance || std::fabs(transform(3, 1)) > tolerance.RecompositionTolerance || std::fabs(transform(3, 2)) > tolerance.RecompositionTolerance || std::fabs(transform(3, 3) - 1.f) > tolerance.RecompositionTolerance)
            return false;

        const Vec3f column_x = {transform(0, 0), transform(1, 0), transform(2, 0)};
        const Vec3f column_y = {transform(0, 1), transform(1, 1), transform(2, 1)};
        const Vec3f column_z = {transform(0, 2), transform(1, 2), transform(2, 2)};
        const float length_x = column_x.magnitude();
        const float length_y = column_y.magnitude();
        const float length_z = column_z.magnitude();
        if (!finite(length_x) || !finite(length_y) || !finite(length_z) || length_x <= tolerance.MinimumScale || length_y <= tolerance.MinimumScale || length_z <= tolerance.MinimumScale)
            return false;

        const Vec3f unit_x = column_x / length_x;
        const Vec3f unit_y = column_y / length_y;
        const Vec3f unit_z = column_z / length_z;
        const auto  dot    = [](const Vec3f& a, const Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        const auto  cross  = [](const Vec3f& a, const Vec3f& b) { return Vec3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };

        if (std::fabs(dot(unit_x, unit_y)) > tolerance.OrthogonalityTolerance || std::fabs(dot(unit_x, unit_z)) > tolerance.OrthogonalityTolerance || std::fabs(dot(unit_y, unit_z)) > tolerance.OrthogonalityTolerance)
            return false;

        const float determinant = dot(unit_x, cross(unit_y, unit_z));
        if (!finite(determinant) || std::fabs(std::fabs(determinant) - 1.f) > tolerance.OrthogonalityTolerance * 3.f)
            return false;

        Vec3f       candidate_scale = {determinant < 0.f ? -length_x : length_x, length_y, length_z};
        const Vec3f rotation_x      = column_x / candidate_scale.x;
        const Vec3f rotation_y      = column_y / candidate_scale.y;
        const Vec3f rotation_z      = column_z / candidate_scale.z;

        Mat4f       rotation_matrix = {};
        rotation_matrix(0, 0)       = rotation_x.x;
        rotation_matrix(1, 0)       = rotation_x.y;
        rotation_matrix(2, 0)       = rotation_x.z;
        rotation_matrix(0, 1)       = rotation_y.x;
        rotation_matrix(1, 1)       = rotation_y.y;
        rotation_matrix(2, 1)       = rotation_y.z;
        rotation_matrix(0, 2)       = rotation_z.x;
        rotation_matrix(1, 2)       = rotation_z.y;
        rotation_matrix(2, 2)       = rotation_z.z;

        Vec3f candidate_rotation    = {};
        candidate_rotation.y        = std::asin(clamp(-rotation_matrix(0, 2), -1.f, 1.f));
        const float cos_y           = std::cos(candidate_rotation.y);
        if (std::fabs(cos_y) > tolerance.MinimumScale)
        {
            candidate_rotation.x = std::atan2(rotation_matrix(1, 2), rotation_matrix(2, 2));
            candidate_rotation.z = std::atan2(rotation_matrix(0, 1), rotation_matrix(0, 0));
        }
        else
        {
            // At the YXZ gimbal singularity Z is canonicalized to zero. The remaining
            // degree of freedom is represented by X so the resulting matrix is stable.
            candidate_rotation.x = rotation_matrix(0, 2) < 0.f ? std::atan2(rotation_matrix(1, 0), rotation_matrix(1, 1)) : std::atan2(-rotation_matrix(1, 0), rotation_matrix(1, 1));
            candidate_rotation.z = 0.f;
        }

        const Vec3f candidate_translation = {transform(0, 3), transform(1, 3), transform(2, 3)};
        const Mat4f recomposed            = ComposeTransformMatrix(candidate_translation, candidate_rotation, candidate_scale);
        for (size_t row = 0; row < 4; ++row)
        {
            for (size_t column = 0; column < 4; ++column)
            {
                const float magnitude = std::fmax(1.f, std::fmax(std::fabs(transform(row, column)), std::fabs(recomposed(row, column))));
                if (std::fabs(transform(row, column) - recomposed(row, column)) > tolerance.RecompositionTolerance * magnitude)
                    return false;
            }
        }

        translation = candidate_translation;
        rotation    = candidate_rotation;
        scale       = candidate_scale;
        return true;
    }

    /// @deprecated Use TryDecomposeTransformTRS. This retained spelling now has the same
    ///             strict failure semantics so existing callers cannot silently accept shear.
    inline bool DecomposeTransformComponent(const Mat4f& transform, Vec3f& translation, Vec3f& rotation, Vec3f& scale)
    {
        return TryDecomposeTransformTRS(transform, translation, rotation, scale);
    }

    // Inverse of DecomposeTransformComponent: builds a column-major TRS matrix
    // from position, Euler angles (radians, YXZ order), and scale.
    inline Mat4f ComposeTransformMatrix(const Vec3f& position, const Vec3f& rotation, const Vec3f& scale)
    {
        float sx = sinf(rotation.x), cx = cosf(rotation.x);
        float sy = sinf(rotation.y), cy = cosf(rotation.y);
        float sz = sinf(rotation.z), cz = cosf(rotation.z);

        Mat4f m = {};
        // Row 0
        m(0, 0) = scale.x * (cy * cz);
        m(0, 1) = scale.y * (cy * sz);
        m(0, 2) = scale.z * (-sy);
        m(0, 3) = position.x;
        // Row 1
        m(1, 0) = scale.x * (sx * sy * cz - cx * sz);
        m(1, 1) = scale.y * (sx * sy * sz + cx * cz);
        m(1, 2) = scale.z * (sx * cy);
        m(1, 3) = position.y;
        // Row 2
        m(2, 0) = scale.x * (cx * sy * cz + sx * sz);
        m(2, 1) = scale.y * (cx * sy * sz - sx * cz);
        m(2, 2) = scale.z * (cx * cy);
        m(2, 3) = position.z;
        // Row 3
        m(3, 3) = 1.0f;
        return m;
    }

    template <typename T>
    inline Mat4<T> translate(const Mat4<T>& matrix, const Vec3<T>& translation)
    {
        Mat4<T> result = matrix;

        // Set translation column (last column)
        result(0, 3)   = matrix(0, 0) * translation.x + matrix(0, 1) * translation.y + matrix(0, 2) * translation.z + matrix(0, 3);
        result(1, 3)   = matrix(1, 0) * translation.x + matrix(1, 1) * translation.y + matrix(1, 2) * translation.z + matrix(1, 3);
        result(2, 3)   = matrix(2, 0) * translation.x + matrix(2, 1) * translation.y + matrix(2, 2) * translation.z + matrix(2, 3);
        result(3, 3)   = matrix(3, 0) * translation.x + matrix(3, 1) * translation.y + matrix(3, 2) * translation.z + matrix(3, 3);

        return result;
    }

    template <typename T, size_t R, size_t C>
    const T* value_ptr(const Matrix<T, R, C>& matrix)
    {
        return &matrix.m_data[0][0];
    }

    template <typename T, size_t R, size_t C>
    T* value_ptr(Matrix<T, R, C>& matrix)
    {
        return &matrix.m_data[0][0];
    }

    // value_ptr for vectors
    template <typename T>
    const T* value_ptr(const Vec2<T>& vec)
    {
        return &vec.x;
    }

    template <typename T>
    T* value_ptr(Vec2<T>& vec)
    {
        return &vec.x;
    }

    template <typename T>
    const T* value_ptr(const Vec3<T>& vec)
    {
        return &vec.x;
    }

    template <typename T>
    T* value_ptr(Vec3<T>& vec)
    {
        return &vec.x;
    }

    template <typename T>
    const T* value_ptr(const Vec4<T>& vec)
    {
        return &vec.x;
    }

    template <typename T>
    T* value_ptr(Vec4<T>& vec)
    {
        return &vec.x;
    }

} // namespace ZEngine::Core::Maths
