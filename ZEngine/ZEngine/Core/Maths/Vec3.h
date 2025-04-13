#pragma once

#include <cmath>

namespace ZEngine::Core::Maths
{

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        Vec3()  = default;
        Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

        Vec3 operator-() const
        {
            return Vec3(-x, -y, -z);
        }

        Vec3 operator+(const Vec3& rhs) const
        {
            return Vec3(x + rhs.x, y + rhs.y, z + rhs.z);
        }
        Vec3 operator-(const Vec3& rhs) const
        {
            return Vec3(x - rhs.x, y - rhs.y, z - rhs.z);
        }
        Vec3 operator*(const Vec3& rhs) const
        {
            return Vec3(x * rhs.x, y * rhs.y, z * rhs.z);
        }
        Vec3 operator/(const Vec3& rhs) const
        {
            return Vec3(x / rhs.x, y / rhs.y, z / rhs.z);
        }

        Vec3 operator*(float scalar) const
        {
            return Vec3(x * scalar, y * scalar, z * scalar);
        }
        Vec3 operator/(float scalar) const
        {
            return Vec3(x / scalar, y / scalar, z / scalar);
        }

        Vec3& operator+=(const Vec3& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            z += rhs.z;
            return *this;
        }
        Vec3& operator-=(const Vec3& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            z -= rhs.z;
            return *this;
        }
        Vec3& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }
        Vec3& operator/=(float scalar)
        {
            x /= scalar;
            y /= scalar;
            z /= scalar;
            return *this;
        }

        bool operator==(const Vec3& rhs) const
        {
            return x == rhs.x && y == rhs.y && z == rhs.z;
        }
        bool operator!=(const Vec3& rhs) const
        {
            return !(*this == rhs);
        }

        float length() const
        {
            return std::sqrt(x * x + y * y + z * z);
        }
        float length_squared() const
        {
            return x * x + y * y + z * z;
        }

        Vec3 normalized() const
        {
            float len = length();
            return len > 0.0f ? (*this / len) : Vec3(0.0f, 0.0f, 0.0f);
        }

        void normalize()
        {
            float len = length();
            if (len > 0.0f)
            {
                x /= len;
                y /= len;
                z /= len;
            }
        }

        static float dot(const Vec3& a, const Vec3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        static Vec3 cross(const Vec3& a, const Vec3& b)
        {
            return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        }
    };
} // namespace ZEngine::Core::Maths