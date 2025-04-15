#pragma once
#include <cmath>

namespace ZEngine::Core::Maths
{
    template <typename T, size_t N, typename Derived>
    struct Vector
    {
        T& operator[](size_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < N, "Index out of range");
            return m_data[index];
        }

        const T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < N, "Index out of range");
            return m_data[index];
        }

        size_t size() const
        {
            return N;
        }

        static bool IsEqual(T a, T b)
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                constexpr T epsilon = static_cast<T>(1e-6);
                return std::fabs(a - b) < epsilon;
            }
            else
            {
                return a == b;
            }
        }

        bool operator==(const Derived& other) const
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (!IsEqual(m_data[i], other.m_data[i]))
                    return false;
            }
            return true;
        }

        bool operator!=(const Derived& other) const
        {
            return !(*this == other);
        }

        bool operator>(const Derived& other) const
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (m_data[i] > other.m_data[i])
                    return true;
                if (m_data[i] < other.m_data[i])
                    return false;
            }
            return false;
        }

        bool operator<(const Derived& other) const
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (m_data[i] < other.m_data[i])
                    return true;
                if (m_data[i] > other.m_data[i])
                    return false;
            }
            return false;
        }

        Derived& operator+=(const Derived& other)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] += other[i];
            }
            return *this;
        }

        Derived& operator-=(const Derived& other)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] -= other[i];
            }
            return *this;
        }

        Derived operator*(T scalar) const
        {
            Derived result;
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] * scalar;
            }
            return result;
        }

        Derived operator/(T scalar) const
        {
            Derived result;
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] / scalar;
            }
            return result;
        }

        Derived& operator*=(T scalar)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] *= scalar;
            }
            return *this;
        }

        Derived& operator/=(T scalar)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] /= scalar;
            }
            return *this;
        }
        Derived operator+(const Derived& other) const
        {
            Derived result;
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] + other[i];
            }
            return result;
        }

        Derived operator-(const Derived& other) const
        {
            Derived result;

            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] - other[i];
            }
            return result;
        }

        Derived operator*(const Derived& other) const
        {
            Derived result;
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] * other[i];
            }
            return result;
        }

        T dot(const Derived& other) const
        {
            T result = T();
            for (size_t i = 0; i < N; ++i)
            {
                result += m_data[i] * other[i];
            }
            return result;
        }

        T magnitude() const
        {
            const Derived& self = static_cast<const Derived&>(*this);
            return std::sqrt(self.dot(self));
        }

        Derived normalized() const
        {
            Derived result;
            T       len = magnitude();
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] / len;
            }
            return result;
        }

        T cross2d(const Derived& other) const
        {
            ZENGINE_VALIDATE_ASSERT(N == 2, "cross2d() is only defined for 2D vectors.");
            return (m_data[0] * other[1]) - (m_data[1] * other[0]);
        }

        Derived cross3d(const Derived& other) const
        {
            ZENGINE_VALIDATE_ASSERT(N == 3, "cross3d() is only defined for 3D vectors.");
            Derived result;

            result[0] = m_data[1] * other[2] - m_data[2] * other[1];
            result[1] = m_data[2] * other[0] - m_data[0] * other[2];
            result[2] = m_data[0] * other[1] - m_data[1] * other[0];
            return result;
        }

        T m_data[N];
    };

    template <typename T>
    struct Vec2 : public Vector<T, 2, Vec2<T>>
    {
        using Base = Vector<T, 2, Vec2<T>>;

        Vec2()     = default;
        Vec2(T x_, T y_)
        {
            x = x_;
            y = y_;
        }

        T& x = Base::m_data[0];
        T& y = Base::m_data[1];
    };

    template <typename T>
    struct Vec3 : public Vector<T, 3, Vec3<T>>
    {
        using Base = Vector<T, 3, Vec3<T>>;

        Vec3()     = default;
        Vec3(T x_, T y_, T z_)
        {
            x = x_;
            y = y_;
            z = z_;
        }

        T& x = Base::m_data[0];
        T& y = Base::m_data[1];
        T& z = Base::m_data[2];
    };

    template <typename T>
    struct Vec4 : public Vector<T, 4, Vec4<T>>
    {
        using Base = Vector<T, 4, Vec4<T>>;

        Vec4()     = default;
        Vec4(T x_, T y_, T z_, T w_)
        {
            x = x_;
            y = y_;
            z = z_;
            w = w_;
        }

        T& x = Base::m_data[0];
        T& y = Base::m_data[1];
        T& z = Base::m_data[2];
        T& w = Base::m_data[3];
    };

} // namespace ZEngine::Core::Maths