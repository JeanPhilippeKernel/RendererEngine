#pragma once
#include <cmath>

namespace ZEngine::Core::Maths
{

    struct Vec4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;

        Vec4()  = default;
        Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

        Vec4 operator-() const
        {
            return Vec4(-x, -y, -z, -w);
        }

        Vec4 operator+(const Vec4& rhs) const
        {
            return Vec4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
        }
        Vec4 operator-(const Vec4& rhs) const
        {
            return Vec4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
        }
        Vec4 operator*(const Vec4& rhs) const
        {
            return Vec4(x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w);
        }
        Vec4 operator/(const Vec4& rhs) const
        {
            return Vec4(x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w);
        }

        Vec4 operator*(float scalar) const
        {
            return Vec4(x * scalar, y * scalar, z * scalar, w * scalar);
        }
        Vec4 operator/(float scalar) const
        {
            return Vec4(x / scalar, y / scalar, z / scalar, w / scalar);
        }

        Vec4& operator+=(const Vec4& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            z += rhs.z;
            w += rhs.w;
            return *this;
        }
        Vec4& operator-=(const Vec4& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            z -= rhs.z;
            w += rhs.w;
            return *this;
        }
        Vec4& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }
        Vec4& operator/=(float scalar)
        {
            x /= scalar;
            y /= scalar;
            z /= scalar;
            w /= scalar;
            return *this;
        }

        bool operator==(const Vec4& rhs) const
        {
            return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
        }
        bool operator!=(const Vec4& rhs) const
        {
            return !(*this == rhs);
        }

        float length() const
        {
            return std::sqrt(x * x + y * y + z * z + w * w);
        }
        float length_squared() const
        {
            return x * x + y * y + z * z + w * w;
        }

        Vec4 normalized() const
        {
            float len = length();
            return len > 0.0f ? (*this / len) : Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }

        void normalize()
        {
            float len = length();
            if (len > 0.0f)
            {
                x /= len;
                y /= len;
                z /= len;
                w /= len;
            }
        }

        static float dot(const Vec4& a, const Vec4& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        }
    };
} // namespace ZEngine::Core::Maths