

#include <ZEngineDef.h>
#include <cstddef>
#include <type_traits>
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
            return RowProxy{&m_data[row * C]};
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

        Matrix<T, R, C> identity()
        {
            Matrix<T, R, C> result{};
            for (size_t i = 0; i < C; ++i)
                result[i][i] = T(1);
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
    };

    template <typename T>
    struct Mat3 : public Matrix<T, 3, 3>
    {

        Mat3()
        {
            for (size_t i = 0; i < 6; ++i)
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

        Mat4& operator+=(const Mat4& other)
        {
            for (size_t i = 0; i < 8; i++)
            {
                this->m_data[i] += other.m_data[i];
            }
            return *this;
        }
        Mat4& operator-=(const Mat4& other)
        {
            for (size_t i = 0; i < 8; i++)
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
    };

} // namespace ZEngine::Core::Maths
