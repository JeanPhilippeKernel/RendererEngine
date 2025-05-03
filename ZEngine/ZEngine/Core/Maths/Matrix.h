#include <cstddef>
#include "Vec.h"

namespace ZEngine::Core::Maths
{
    template <typename T, size_t R, size_t C, typename = std::enable_if_t<std::is_arithmetic_v<T> && (R >= 1) && (C >= 1)>>
    struct Matrix
    {
        T m_data[R * C];

        struct RowProxy
        {
            T* row_data;

            T& operator[](size_t col)
            {
                ZENGINE_VALIDATE_ASSERT(col < C, "Column index out of range");
                return row_data[col];
            }

            const T& operator[](size_t col) const
            {
                ZENGINE_VALIDATE_ASSERT(col < C, "Column index out of range");
                return row_data[col];
            }
        };

        RowProxy operator[](size_t row)
        {
            ZENGINE_VALIDATE_ASSERT(row < R, "Row index out of range");
            return RowProxy{&m_data[row * C]};
        }

        const RowProxy operator[](size_t row) const
        {
            ZENGINE_VALIDATE_ASSERT(row < R, "Row index out of range");
            return RowProxy{const_cast<T*>(&m_data[row * C])};
        }

        Vec<T, C> getRow(size_t row) const
        {
            ZENGINE_VALIDATE_ASSERT(row < R, "Row index out of range");
            Vec<T, C> result;
            for (size_t i = 0; i < C; ++i)
            {
                result[i] = this->m_data[row * C + i];
            }
            return result;
        }

        Vec<T, R> getColumn(size_t col) const
        {
            ZENGINE_VALIDATE_ASSERT(col < C, "Column index out of range");
            Vec<T, R> result;
            for (size_t i = 0; i < R; ++i)
            {
                result[i] = this->m_data[i * C + col];
            }
            return result;
        }

        Matrix<T, R, C> operator+(Matrix<T, R, C>& other)
        {
            Matrix<T, R, C> result{};
            for (size_t i = 0; i < R; ++i)
            {
                for (size_t j = 0; j < C; j++)
                {
                    result[i][j] = other[i][j] + (*this)[i][j];
                }
            }
            return result;
        }

        Matrix<T, R, C> operator-(Matrix<T, R, C>& other)
        {
            Matrix<T, R, C> result{};
            for (size_t i = 0; i < R; ++i)
            {
                for (size_t j = 0; j < C; j++)
                {
                    result[i][j] = (*this)[i][j] - other[i][j];
                }
            }
            return result;
        }
    };

    template <typename T>
    struct Mat2 : public Matrix<T, 2, 2>
    {

        Mat2()
        {
            for (size_t i = 0; i < 4; ++i)
                this->m_data[i] = 0;
        }

        Mat2(T m00, T m01, T m10, T m11)
        {
            this->m_data[0] = m00;
            this->m_data[1] = m01;
            this->m_data[2] = m10;
            this->m_data[3] = m11;
        }

        Mat2(const Matrix<T, 2, 2>& other)
        {
            for (size_t i = 0; i < 4; ++i)
                this->m_data[i] = other.m_data[i];
        }

        T determinant() const
        {
            return this->m_data[0] * this->m_data[3] - this->m_data[1] * this->m_data[2];
        }

        Mat2& operator+=(const Mat2& other)
        {
            for (size_t i = 0; i < 4; i++)
            {
                this->m_data[i] += other.m_data[i];
            }
            return *this;
        }
        Mat2& operator-=(const Mat2& other)
        {
            for (size_t i = 0; i < 4; i++)
            {
                this->m_data[i] -= other.m_data[i];
            }
            return *this;
        }
        Mat2 operator+(const Mat2& other)
        {
            Mat2 result  = *this;
            result      += other;
            return result;
        }
        Mat2 operator-(const Mat2& other)
        {
            Mat2 result  = *this;
            result      -= other;
            return result;
        }
        Mat2 operator*(T scalar)
        {
            Mat2 result = *this;
            for (size_t i = 0; i < 4; i++)
            {
                result.m_data[i] *= scalar;
            }
            return result;
        }
        Mat2& operator*=(T scalar)
        {
            for (size_t i = 0; i < 4; ++i)
            {
                this->m_data[i] *= scalar;
            }
            return *this;
        }

        Mat2 operator/(T scalar) const
        {
            Mat2 result = *this;
            scalar      = 1.0f / scalar;
            for (size_t i = 0; i < 4; i++)
            {
                result.m_data[i] *= scalar;
            }
            return result;
        }

        Mat2& operator/=(T scalar)
        {
            scalar = 1.0f / scalar;
            for (size_t i = 0; i < 4; ++i)
            {
                this->m_data[i] *= scalar;
            }
            return *this;
        }
        Mat2 inverse()
        {
            T det = this->determinant();
            ZENGINE_VALIDATE_ASSERT(det != 0, "Matrix is singular and cannot be inverted");

            T invDet = 1 / det;

            return Mat2<T>(this->m_data[3] * invDet, -this->m_data[1] * invDet, -this->m_data[2] * invDet, this->m_data[0] * invDet);
        }
    };

    template <typename T>
    struct Mat3 : public Matrix<T, 3, 3>
    {

        Mat3()
        {
            for (size_t i = 0; i < 9; ++i)
                this->m_data[i] = 0;
        }

        Mat3(T m00, T m01, T m02, T m10, T m11, T m12, T m20, T m21, T m22)
        {
            this->m_data[0] = m00;
            this->m_data[1] = m01;
            this->m_data[2] = m02;
            this->m_data[3] = m10;
            this->m_data[4] = m11;
            this->m_data[5] = m12;
            this->m_data[6] = m20;
            this->m_data[7] = m21;
            this->m_data[8] = m22;
        }

        Mat3(const Matrix<T, 3, 3>& other)
        {
            for (size_t i = 0; i < 9; ++i)
                this->m_data[i] = other.m_data[i];
        }

        T determinant() const
        {
            return this->m_data[0] * (this->m_data[4] * this->m_data[8] - this->m_data[5] * this->m_data[7]) - this->m_data[1] * (this->m_data[3] * this->m_data[8] - this->m_data[5] * this->m_data[6]) + this->m_data[2] * (this->m_data[3] * this->m_data[7] - this->m_data[4] * this->m_data[6]);
        }

        Mat3& operator+=(const Mat3& other)
        {
            for (size_t i = 0; i < 9; i++)
            {
                this->m_data[i] += other.m_data[i];
            }
            return *this;
        }
        Mat3& operator-=(const Mat3& other)
        {
            for (size_t i = 0; i < 9; i++)
            {
                this->m_data[i] -= other.m_data[i];
            }
            return *this;
        }
        Mat3 operator+(const Mat3& other)
        {
            Mat3 result  = *this;
            result      += other;
            return result;
        }
        Mat3 operator-(const Mat3& other)
        {
            Mat3 result  = *this;
            result      -= other;
            return result;
        }
        Mat3 operator*(T scalar)
        {
            Mat3 result = *this;
            for (size_t i = 0; i < 9; i++)
            {
                result.m_data[i] *= scalar;
            }
            return result;
        }
        Mat3& operator*=(T scalar)
        {
            for (size_t i = 0; i < 9; ++i)
            {
                this->m_data[i] *= scalar;
            }
            return *this;
        }

        Mat3 operator/(T scalar) const
        {
            Mat3 result = *this;
            scalar      = 1.0f / scalar;
            for (size_t i = 0; i < 9; i++)
            {
                result.m_data[i] *= scalar;
            }
            return result;
        }
        Mat3& operator/=(T scalar)
        {
            scalar = 1.0f / scalar;
            for (size_t i = 0; i < 9; ++i)
            {
                this->m_data[i] *= scalar;
            }
            return *this;
        }

        Mat3 inverse()
        {
            T det = this->determinant();
            ZENGINE_VALIDATE_ASSERT(det != 0, "Matrix is singular and cannot be inverted");

            T invDet = 1 / det;

            return Mat3<T>(
                (this->m_data[4] * this->m_data[8] - this->m_data[5] * this->m_data[7]) * invDet,
                -(this->m_data[1] * this->m_data[8] - this->m_data[2] * this->m_data[7]) * invDet,
                (this->m_data[1] * this->m_data[5] - this->m_data[2] * this->m_data[4]) * invDet,
                -(this->m_data[3] * this->m_data[8] - this->m_data[5] * this->m_data[6]) * invDet,
                (this->m_data[0] * this->m_data[8] - this->m_data[2] * this->m_data[6]) * invDet,
                -(this->m_data[0] * this->m_data[5] - this->m_data[2] * this->m_data[3]) * invDet,
                (this->m_data[3] * this->m_data[7] - this->m_data[4] * this->m_data[6]) * invDet,
                -(this->m_data[0] * this->m_data[7] - this->m_data[1] * this->m_data[6]) * invDet,
                (this->m_data[0] * this->m_data[4] - this->m_data[1] * this->m_data[3]) * invDet);
        }
    };

    template <typename T>
    struct Mat4 : public Matrix<T, 4, 4>
    {

        Mat4()
        {
            for (size_t i = 0; i < 16; ++i)
                this->m_data[i] = 0;
        }

        Mat4(T m00, T m01, T m02, T m03, T m10, T m11, T m12, T m13, T m20, T m21, T m22, T m23, T m30, T m31, T m32, T m33)
        {
            this->m_data[0]  = m00;
            this->m_data[1]  = m01;
            this->m_data[2]  = m02;
            this->m_data[3]  = m03;
            this->m_data[4]  = m10;
            this->m_data[5]  = m11;
            this->m_data[6]  = m12;
            this->m_data[7]  = m13;
            this->m_data[8]  = m20;
            this->m_data[9]  = m21;
            this->m_data[10] = m22;
            this->m_data[11] = m23;
            this->m_data[12] = m30;
            this->m_data[13] = m31;
            this->m_data[14] = m32;
            this->m_data[15] = m33;
        }

        Mat4(const Matrix<T, 4, 4>& other)
        {
            for (size_t i = 0; i < 16; ++i)
                this->m_data[i] = other.m_data[i];
        }

        Mat4& operator+=(const Mat4& other)
        {
            for (size_t i = 0; i < 16; i++)
            {
                this->m_data[i] += other.m_data[i];
            }
            return *this;
        }
        Mat4& operator-=(const Mat4& other)
        {
            for (size_t i = 0; i < 16; i++)
            {
                this->m_data[i] -= other.m_data[i];
            }
            return *this;
        }
        Mat4 operator+(const Mat4& other)
        {
            Mat4 result  = *this;
            result      += other;
            return result;
        }
        Mat4 operator-(const Mat4& other)
        {
            Mat4 result  = *this;
            result      -= other;
            return result;
        }
        Mat4 operator*(T scalar)
        {
            Mat4 result = *this;
            for (size_t i = 0; i < 16; i++)
            {
                result.m_data[i] *= scalar;
            }
            return result;
        }
        Mat4& operator*=(T scalar)
        {

            for (size_t i = 0; i < 16; ++i)
            {
                this->m_data[i] *= scalar;
            }
            return *this;
        }

        Mat4 operator/(T scalar) const
        {
            Mat4 result = *this;
            scalar      = 1.0f / scalar;
            for (size_t i = 0; i < 16; i++)
            {
                result.m_data[i] *= scalar;
            }
            return result;
        }
        Mat4& operator/=(T scalar)
        {
            scalar = 1.0f / scalar;
            for (size_t i = 0; i < 16; ++i)
            {
                this->m_data[i] *= scalar;
            }
            return *this;
        }

        T determinant() const
        {
            return (
                this->m_data[0] * (this->m_data[5] * (this->m_data[10] * this->m_data[15] - this->m_data[11] * this->m_data[14]) - this->m_data[6] * (this->m_data[9] * this->m_data[15] - this->m_data[11] * this->m_data[13]) + this->m_data[7] * (this->m_data[9] * this->m_data[14] - this->m_data[10] * this->m_data[13])) -
                this->m_data[1] * (this->m_data[4] * (this->m_data[10] * this->m_data[15] - this->m_data[11] * this->m_data[14]) - this->m_data[6] * (this->m_data[8] * this->m_data[15] - this->m_data[11] * this->m_data[12]) + this->m_data[7] * (this->m_data[8] * this->m_data[14] - this->m_data[10] * this->m_data[12])) +
                this->m_data[2] * (this->m_data[4] * (this->m_data[9] * this->m_data[15] - this->m_data[11] * this->m_data[13]) - this->m_data[5] * (this->m_data[8] * this->m_data[15] - this->m_data[11] * this->m_data[12]) + this->m_data[7] * (this->m_data[8] * this->m_data[13] - this->m_data[9] * this->m_data[12])) -
                this->m_data[3] * (this->m_data[4] * (this->m_data[9] * this->m_data[14] - this->m_data[10] * this->m_data[13]) - this->m_data[5] * (this->m_data[8] * this->m_data[14] - this->m_data[10] * this->m_data[12]) + this->m_data[6] * (this->m_data[8] * this->m_data[13] - this->m_data[9] * this->m_data[12])));
        }
        Mat4<T> inverse() const
        {
            const T* m   = this->m_data;

            T        c00 = m[5] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[9] * m[15] - m[11] * m[13]) + m[7] * (m[9] * m[14] - m[10] * m[13]);
            T        c01 = -(m[4] * (m[10] * m[15] - m[11] * m[14]) - m[6] * (m[8] * m[15] - m[11] * m[12]) + m[7] * (m[8] * m[14] - m[10] * m[12]));
            T        c02 = m[4] * (m[9] * m[15] - m[11] * m[13]) - m[5] * (m[8] * m[15] - m[11] * m[12]) + m[7] * (m[8] * m[13] - m[9] * m[12]);
            T        c03 = -(m[4] * (m[9] * m[14] - m[10] * m[13]) - m[5] * (m[8] * m[14] - m[10] * m[12]) + m[6] * (m[8] * m[13] - m[9] * m[12]));

            T        c10 = -(m[1] * (m[10] * m[15] - m[11] * m[14]) - m[2] * (m[9] * m[15] - m[11] * m[13]) + m[3] * (m[9] * m[14] - m[10] * m[13]));
            T        c11 = m[0] * (m[10] * m[15] - m[11] * m[14]) - m[2] * (m[8] * m[15] - m[11] * m[12]) + m[3] * (m[8] * m[14] - m[10] * m[12]);
            T        c12 = -(m[0] * (m[9] * m[15] - m[11] * m[13]) - m[1] * (m[8] * m[15] - m[11] * m[12]) + m[3] * (m[8] * m[13] - m[9] * m[12]));
            T        c13 = m[0] * (m[9] * m[14] - m[10] * m[13]) - m[1] * (m[8] * m[14] - m[10] * m[12]) + m[2] * (m[8] * m[13] - m[9] * m[12]);

            T        c20 = m[1] * (m[6] * m[15] - m[7] * m[14]) - m[2] * (m[5] * m[15] - m[7] * m[13]) + m[3] * (m[5] * m[14] - m[6] * m[13]);
            T        c21 = -(m[0] * (m[6] * m[15] - m[7] * m[14]) - m[2] * (m[4] * m[15] - m[7] * m[12]) + m[3] * (m[4] * m[14] - m[6] * m[12]));
            T        c22 = m[0] * (m[5] * m[15] - m[7] * m[13]) - m[1] * (m[4] * m[15] - m[7] * m[12]) + m[3] * (m[4] * m[13] - m[5] * m[12]);
            T        c23 = -(m[0] * (m[5] * m[14] - m[6] * m[13]) - m[1] * (m[4] * m[14] - m[6] * m[12]) + m[2] * (m[4] * m[13] - m[5] * m[12]));

            T        c30 = -(m[1] * (m[6] * m[11] - m[7] * m[10]) - m[2] * (m[5] * m[11] - m[7] * m[9]) + m[3] * (m[5] * m[10] - m[6] * m[9]));
            T        c31 = m[0] * (m[6] * m[11] - m[7] * m[10]) - m[2] * (m[4] * m[11] - m[7] * m[8]) + m[3] * (m[4] * m[10] - m[6] * m[8]);
            T        c32 = -(m[0] * (m[5] * m[11] - m[7] * m[9]) - m[1] * (m[4] * m[11] - m[7] * m[8]) + m[3] * (m[4] * m[9] - m[5] * m[8]));
            T        c33 = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);

            T        det = m[0] * c00 + m[1] * c01 + m[2] * c02 + m[3] * c03;
            ZENGINE_VALIDATE_ASSERT(det != 0, "Matrix is singular and cannot be inverted");

            T       invDet = static_cast<T>(1) / det;

            Mat4<T> result;
            result.m_data[0]  = c00 * invDet;
            result.m_data[1]  = c10 * invDet;
            result.m_data[2]  = c20 * invDet;
            result.m_data[3]  = c30 * invDet;

            result.m_data[4]  = c01 * invDet;
            result.m_data[5]  = c11 * invDet;
            result.m_data[6]  = c21 * invDet;
            result.m_data[7]  = c31 * invDet;

            result.m_data[8]  = c02 * invDet;
            result.m_data[9]  = c12 * invDet;
            result.m_data[10] = c22 * invDet;
            result.m_data[11] = c32 * invDet;

            result.m_data[12] = c03 * invDet;
            result.m_data[13] = c13 * invDet;
            result.m_data[14] = c23 * invDet;
            result.m_data[15] = c33 * invDet;

            return result;
        }
    };

    using IMat2 = Mat2<int>;
    using Mat2f = Mat2<float>;
    using Mat3f = Mat3<float>;
    using Mat4f = Mat4<float>;

    template <typename T>
    T identity();

    template <>
    inline Mat2f identity<Mat2f>()
    {
        return Mat2f(1, 0, 0, 1);
    }

    template <>
    inline Mat3f identity<Mat3f>()
    {
        return Mat3f(1, 0, 0, 0, 1, 0, 0, 0, 1);
    }

    template <>
    inline Mat4f identity<Mat4f>()
    {
        return Mat4f(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    }

    template <typename T, size_t R, size_t K, size_t C>
    Matrix<T, R, C> operator*(const Matrix<T, R, K>& a, const Matrix<T, K, C>& b)
    {
        Matrix<T, R, C> result{};
        for (size_t i = 0; i < R; ++i)
        {
            for (size_t j = 0; j < C; ++j)
            {
                T sum = T{};
                for (size_t k = 0; k < K; ++k)
                {
                    sum += a[i][k] * b[k][j];
                }
                result[i][j] = sum;
            }
        }
        return result;
    }

} // namespace ZEngine::Core::Maths
