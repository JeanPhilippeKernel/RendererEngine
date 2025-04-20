#pragma once
#include <ZEngineDef.h>
#include <cmath>

namespace ZEngine::Core::Maths
{
    template <typename T, size_t N, typename Derived>
    struct Vec
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
            return a == b;
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

        T magnitude() const
        {
            const Derived& self = static_cast<const Derived&>(*this);
            return std::sqrt(dot(self, self));
        }

        Derived normalize() const
        {
            Derived result;
            T       len = magnitude();
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] / len;
            }
            return result;
        }

        T m_data[N];
    };

    template <typename T>
    struct Vec2 : public Vec<T, 2, Vec2<T>>
    {
        using Base = Vec<T, 2, Vec2<T>>;

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
    struct Vec3 : public Vec<T, 3, Vec3<T>>
    {
        using Base = Vec<T, 3, Vec3<T>>;

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
    struct Vec4 : public Vec<T, 4, Vec4<T>>
    {
        using Base = Vec<T, 4, Vec4<T>>;

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

    template <typename T>
    inline Vec3<T> cross3d(const Vec3<T>& a, const Vec3<T>& b)
    {
        return Vec3<T>{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    template <typename T>
    inline T cross2d(const Vec2<T>& a, const Vec2<T>& b)
    {
        return (a.x * b.y) - (a.y * b.x);
    }

    template <typename T, size_t N, typename Derived>
    inline T dot(const Vec<T, N, Derived>& a, const Vec<T, N, Derived>& b)
    {
        T result = T();
        for (size_t i = 0; i < N; ++i)
        {
            result += a[i] * b[i];
        }
        return result;
    }

} // namespace ZEngine::Core::Maths