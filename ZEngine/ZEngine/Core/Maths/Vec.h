#pragma once
#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/ZEngineDef.h>
#include <type_traits>

namespace ZEngine::Core::Maths
{
    template <typename T, size_t N, typename = std::enable_if_t<std::is_arithmetic_v<T> && (N >= 1)>>
    struct Vec
    {
        T& operator[](size_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < N, "Index out of range");
            return (&x)[index];
        }

        const T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < N, "Index out of range");
            return (&x)[index];
        }
        static bool IsEqual(const T& a, const T& b, T tolerance = std::numeric_limits<T>::epsilon())
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                return abs(a - b) <= tolerance * max(abs(a), abs(b));
            }
            return a == b;
        }

        static bool IsEqual(const Vec& a, const Vec& b)
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (!IsEqual(a[i], b[i]))
                {
                    return false;
                }
            }
            return true;
        }

        T x, y, z, w;
    };

    template <typename T>
    struct Vec2 : public Vec<T, 2>
    {

        using Vec<T, 2>::x;
        using Vec<T, 2>::y;

        Vec2() = default;
        Vec2(T x_, T y_)
        {
            x = x_;
            y = y_;
        }

        bool operator!=(const Vec2& other) const
        {
            return !(*this == other);
        }

        Vec2 operator+(const Vec2& other) const
        {
            return Vec2(x + other.x, y + other.y);
        }
        Vec2 operator-(const Vec2& other) const
        {
            return Vec2(x - other.x, y - other.y);
        }
        Vec2 operator*(T scalar) const
        {
            return Vec2(x * scalar, y * scalar);
        }
        Vec2 operator/(T scalar) const
        {
            scalar = 1.0f / scalar;
            return Vec2(x * scalar, y * scalar);
        }

        bool operator==(const Vec2& other) const
        {
            auto& base = Vec<T, 2>::IsEqual;
            return base(x, other.x) && base(y, other.y);
        }

        Vec2& operator+=(const Vec2& other)
        {
            x += other.x;
            y += other.y;
            return *this;
        }
        Vec2& operator-=(const Vec2& other)
        {
            x -= other.x;
            y -= other.y;
            return *this;
        }
        Vec2& operator*=(T scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }
        Vec2& operator/=(T scalar)
        {
            scalar  = 1.0f / scalar;
            x      *= scalar;
            y      *= scalar;
            return *this;
        }

        T magnitude() const
        {
            return sqrt(x * x + y * y);
        }

        Vec2 normalize() const
        {
            T len = magnitude();
            ZENGINE_VALIDATE_ASSERT(len != 0, "Cannot normalize zero vector");
            return *this / len;
        }
    };

    template <typename T>
    struct Vec3 : public Vec<T, 3>
    {
        using Vec<T, 3>::x;
        using Vec<T, 3>::y;
        using Vec<T, 3>::z;

        Vec3() = default;
        Vec3(T x_, T y_, T z_)
        {
            x = x_;
            y = y_;
            z = z_;
        }

        T& operator[](size_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < 3, "Index out of range");
            return (&x)[index];
        }

        const T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < 3, "Index out of range");
            return (&x)[index];
        }

        bool operator==(const Vec3& other) const
        {
            auto& base = Vec<T, 3>::IsEqual;
            return base(x, other.x) && base(y, other.y) && base(z, other.z);
        }

        bool operator!=(const Vec3& other) const
        {
            return !(*this == other);
        }

        Vec3 operator+(const Vec3& other) const
        {
            return Vec3(x + other.x, y + other.y, z + other.z);
        }
        Vec3 operator-(const Vec3& other) const
        {
            return Vec3(x - other.x, y - other.y, z - other.z);
        }
        Vec3 operator*(T scalar) const
        {
            return Vec3(x * scalar, y * scalar, z * scalar);
        }
        Vec3 operator/(T scalar) const
        {
            scalar = 1.0f / scalar;
            return Vec3(x * scalar, y * scalar, z * scalar);
        }

        Vec3& operator+=(const Vec3& other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            return *this;
        }
        Vec3& operator-=(const Vec3& other)
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            return *this;
        }
        Vec3& operator*=(T scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }
        Vec3& operator/=(T scalar)
        {
            scalar  = 1.0f / scalar;
            x      *= scalar;
            y      *= scalar;
            z      *= scalar;
            return *this;
        }

        Vec3 operator-() const
        {
            return Vec3(-x, -y, -z);
        }

        T magnitude() const
        {
            return sqrt(x * x + y * y + z * z);
        }

        Vec3 normalize() const
        {
            T len = magnitude();
            ZENGINE_VALIDATE_ASSERT(len != 0, "Cannot normalize zero vector");
            return *this / len;
        }
    };

    template <typename T>
    struct Vec4 : public Vec<T, 4>
    {
        using Vec<T, 4>::x;
        using Vec<T, 4>::y;
        using Vec<T, 4>::z;
        using Vec<T, 4>::w;

        Vec4() = default;
        Vec4(T x_, T y_, T z_, T w_)
        {
            x = x_;
            y = y_;
            z = z_;
            w = w_;
        }

        Vec4(const Vec3<T>& vec3, T w_)
        {
            x = vec3.x;
            y = vec3.y;
            z = vec3.z;
            w = w_;
        }

        T& operator[](size_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < 4, "Index out of range");
            return (&x)[index];
        }

        const T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < 4, "Index out of range");
            return (&x)[index];
        }

        bool operator==(const Vec4& other) const
        {
            auto& base = Vec<T, 4>::IsEqual;
            return base(x, other.x) && base(y, other.y) && base(z, other.z) && base(w, other.w);
        }

        bool operator!=(const Vec4& other) const
        {
            return !(*this == other);
        }

        Vec4 operator+(const Vec4& other) const
        {
            return Vec4(x + other.x, y + other.y, z + other.z, w + other.w);
        }
        Vec4 operator-(const Vec4& other) const
        {
            return Vec4(x - other.x, y - other.y, z - other.z, w - other.w);
        }
        Vec4 operator*(T scalar) const
        {
            return Vec4(x * scalar, y * scalar, z * scalar, w * scalar);
        }
        Vec4 operator/(T scalar) const
        {
            scalar = 1.0f / scalar;
            return Vec4(x * scalar, y * scalar, z * scalar, w * scalar);
        }

        Vec4& operator+=(const Vec4& other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            w += other.w;
            return *this;
        }
        Vec4& operator-=(const Vec4& other)
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            w -= other.w;
            return *this;
        }
        Vec4& operator*=(T scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            w *= scalar;
            return *this;
        }
        Vec4& operator/=(T scalar)
        {
            scalar  = 1.0f / scalar;
            x      *= scalar;
            y      *= scalar;
            z      *= scalar;
            w      *= scalar;
            return *this;
        }

        T magnitude() const
        {
            return sqrt(x * x + y * y + z * z + w * w);
        }

        Vec4 normalize() const
        {
            T len = magnitude();
            ZENGINE_VALIDATE_ASSERT(len != 0, "Cannot normalize zero vector");
            return *this / len;
        }
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

    template <typename T, size_t N>
    inline T dot(const Vec<T, N>& a, const Vec<T, N>& b)
    {
        T result = T();
        for (size_t i = 0; i < N; ++i)
        {
            result += a[i] * b[i];
        }
        return result;
    }

    template <typename T>
    inline Vec3<T> radians(const Vec3<T>& degrees)
    {
        return Vec3<T>(radians(degrees.x), radians(degrees.y), radians(degrees.z));
    }

    template <typename T>
    inline Vec3<T> degrees(const Vec3<T>& angles)
    {
        return Vec3<T>(degrees(angles.x), degrees(angles.y), degrees(angles.z));
    }

    using Vec2f = Vec2<float>;
    using Vec3f = Vec3<float>;
    using Vec4f = Vec4<float>;

    using IVec2 = Vec2<int>;

} // namespace ZEngine::Core::Maths