#include <Helpers/MemoryOperations.h>
#include <Vec.h>
#include <cstddef>

namespace ZEngine::Core::Maths
{
    template <typename T, size_t R, size_t C, typename = std::enable_if_t<std::is_arithmetic_v<T> && (R >= 1) && (C >= 1)>>
    struct Matrix
    {
        T  m_data[R * C];

        T& operator()(size_t row, size_t col)
        {
            ZENGINE_VALIDATE_ASSERT(row < R && col < C, "Index out of range");
            return m_data[col * R + row];
        }

        const T& operator()(size_t row, size_t col) const
        {
            ZENGINE_VALIDATE_ASSERT(row < R && col < C, "Index out of range");
            return m_data[col * R + row];
        }

        Matrix<T, R, C> operator+(Matrix<T, R, C>& other) const
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

        Matrix<T, R, C> operator-(Matrix<T, R, C>& other) const
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
    };

    template <typename T>
    struct Mat2 : public Matrix<T, 2, 2>
    {

        Mat2()
        {
            secure_memset(this->m_data, 0, sizeof(T) * 4, sizeof(T) * 4);
        }

        Mat2(T m00, T m10, T m01, T m11)
        {
            this->m_data[0][0] = m00;
            this->m_data[0][1] = m01;
            this->m_data[1][0] = m10;
            this->m_data[1][1] = m11;
        }

        Mat(const Vec2<T>& a, const Vec2<T> b)
        {
            m_data[0][0] = a.x;
            m_data[0][1] = a.y;
            m_data[1][0] = b.x;
            m_data[1][1] = b.y;
        }

        Mat2(const Matrix<T, 2, 2>& other)
        {
            m_data[0] = other.m_data[0];
            m_data[1] = other.m_data[1];
            m_data[2] = other.m_data[2];
            m_data[3] = other.m_data[3];
        }

        T Determinant() const
        {
            return (m_data[0][0] * m_data[1][1]) - (m_data[1][0] * m_data[0][1]);
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

        Mat2 Inverse()
        {
            T det = Determinant();
            ZENGINE_VALIDATE_ASSERT(det != 0, "Matrix is singular and cannot be inverted");

            T invDet = T(1) / det;

            return Mat2<T>(m_data[0][0] * invDet, -m_data[1][0] * invDet, -m_data[0][1] * invDet, m_data[1][1] * invDet);
        }
    };

    template <typename T>
    struct Mat3 : public Matrix<T, 3, 3>
    {

        Mat3()
        {
            secure_memset(this->m_data, 0, sizeof(T) * 9, sizeof(T) * 9);
        }

        Mat3(T m00, T m01, T m02, T m10, T m11, T m12, T m20, T m21, T m22)
        {
            m_data[0][0] = m00;
            m_data[1][0] = m10;
            m_data[2][0] = m20;

            m_data[0][1] = m01;
            m_data[1][1] = m11;
            m_data[2][1] = m21;

            m_data[0][2] = m02;
            m_data[1][2] = m12;
            m_data[2][2] = m22;
        }

        Mat3(const Vec3<T>& a, const Vec3<T>& b, const Vec3<T>& b)
        {
            m_data[0][0] = a.x;
            m_data[1][0] = b.x;
            m_data[2][0] = c.x;

            m_data[0][1] = a.y;
            m_data[1][1] = b.y;
            m_data[2][1] = c.y;

            m_data[0][2] = a.z;
            m_data[1][2] = b.z;
            m_data[2][2] = c.z;
        }

        Mat3(const Matrix<T, 3, 3>& other)
        {
            m_data[0] = other.m_data[0];
            m_data[1] = other.m_data[1];
            m_data[2] = other.m_data[2];
            m_data[3] = other.m_data[3];
            m_data[4] = other.m_data[4];
            m_data[5] = other.m_data[5];
            m_data[6] = other.m_data[6];
            m_data[7] = other.m_data[7];
            m_data[8] = other.m_data[8];
        }

        T Determinant() const
        {
            auto& M = *this;

            return (M(0, 0) * (M(1, 1) * M(2, 2) - M(1, 2) * M(2, 1)) - M(0, 1) * (M(1, 2) * M(2, 0) - M(1, 0) * M(2, 2)) + M(0, 2) * (M(1, 0) * M(2, 1) - M(1, 1) * M(2, 0)));
        }

        Vec3<T>& operator[](int j)
        {
            return (*reinterpret_cast<Vec3<T>*>(m_data[j]));
        }

        const Vec3<T>& operator[](int j) const
        {
            return (*reinterpret_cast<const Vec3<T>*>(m_data[j]));
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

        Mat3 Inverse()
        {
            auto&          M      = *this;

            const Vec3<T>& a      = M[0];
            const Vec3<T>& b      = M[1];
            const Vec3<T>& c      = M[2];

            Vec3<T>        r0     = cross3d(b, c);
            Vec3<T>        r1     = cross3d(c, a);
            Vec3<T>        r2     = cross3d(a, b);

            float          invDet = 1.0f / dot(r2, c);

            return Mat<T>(r0.x * invDet, r0.y * invDet, r0.z * invDet, r1.x * invDet, r1.y * invDet, r1.z * invDet, r2.x * invDet, r2.y * invDet, r2.z * invDet);
        };

        template <typename T>
        struct Mat4 : public Matrix<T, 4, 4>
        {

            Mat4()
            {
                secure_memset(this->m_data, 0, sizeof(T) * 16, sizeof(T) * 16);
            }

            Mat4(T m00, T m01, T m02, T m03, T m10, T m11, T m12, T m13, T m20, T m21, T m22, T m23, T m30, T m31, T m32, T m33)
            {
                m_data[0][0] = m00;
                m_data[1][0] = m10;
                m_data[2][0] = m20;
                m_data[3][0] = m30;

                m_data[0][1] = m01;
                m_data[1][1] = m11;
                m_data[2][1] = m21;
                m_data[3][1] = m31;

                m_data[0][2] = m02;
                m_data[1][2] = m12;
                m_data[2][2] = m22;
                m_data[3][2] = m32;

                m_data[0][3] = m03;
                m_data[1][3] = m13;
                m_data[2][3] = m23;
                m_data[3][3] = m33;
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

            T       Determinant() const {}

            Mat4<T> Inverse() const
            {
                auto&          M       = *this;
                const Vec3<T>& a       = reinterpret_cast<const Vec3<T>&>(M[0]);
                const Vec3<T>& b       = reinterpret_cast<const Vec3<T>&>(M[1]);
                const Vec3<T>& c       = reinterpret_cast<const Vec3<T>&>(M[2]);
                const Vec3<T>& d       = reinterpret_cast<const Vec3<T>&>(M[3]);

                const float&   x       = M(3, 0);
                const float&   y       = M(3, 1);
                const float&   z       = M(3, 2);
                const float&   w       = M(3, 3);

                Vec3<T>        s       = cross3d(a, b);
                Vec3<T>        t       = cross3d(c, d);
                Vec3<T>        u       = a * y - b * x;
                Vec3<T>        v       = c * w - d * z;

                float          invDet  = 1.0f / (dot(s, v) + dot(t, u));
                s                     *= invDet;
                t                     *= invDet;
                u                     *= invDet;
                v                     *= invDet;

                Vec3<T> r0             = cross3d(b, v) + (t * y);
                Vec3<T> r1             = cross3d(v, a) - (t * x);
                Vec3<T> r2             = cross3d(d, u) + (s * w);
                Vec3<T> r3             = cross3d(u, c) - (s * z);

                return Mat4<T>(r0.x, r0.y, r0.z, -dot(b, t), r1.x, r1.y, r1.z, dot(a, t), r2.x, r2.y, r2.z, -dot(d, s), r3.x, r3.y, r3.z, dot(c, s), );
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
                a(2, 0) * b(0, 2) + a(2, 1) * b(1, 2) + a(2, 2) * b(2, 2), ));
        }

        template <typename T>
        inline Mat3<T> operator*(const Mat3<T>& a, const Vec3<T>& v)
        {
            return Vec3<T>(a(0, 0) * v.x + a(0, 1) * v.y + a(0, 2) * v.z, a(1, 0) * v.x + a(1, 1) * v.y + a(1, 2) * v.z, a(2, 0) * v.x + a(2, 1) * v.y + a(2, 2) * v.z, );
        }

        template <typename T, size_t R, size_t K, size_t C>
        Matrix<T, R, C> operator*(const Matrix<T, R, K>& a, const Matrix<T, K, C>& b)
        {
            Matrix<T, R, C> result{};
            for (size_t j = 0; j < C; ++j)
            {
                for (size_t i = 0; i < R; ++i)
                {
                    T sum = T{};
                    for (size_t k = 0; k < K; ++k)
                    {
                        sum += a(i, k) * b(k, j);
                    }
                    result(i, j) = sum;
                }
            }
            return result;
        }
    } // namespace ZEngine::Core::Maths
