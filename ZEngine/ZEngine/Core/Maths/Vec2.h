#pragma once

#include <cmath>

namespace ZEngine::Core::Maths
{

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;

        Vec2()  = default;
        Vec2(float x, float y) : x(x), y(y) {}

        // Unary
        Vec2 operator-() const
        {
            return Vec2(-x, -y);
        }

        // Binary arithmetic
        Vec2 operator+(const Vec2& rhs) const
        {
            return Vec2(x + rhs.x, y + rhs.y);
        }
        Vec2 operator-(const Vec2& rhs) const
        {
            return Vec2(x - rhs.x, y - rhs.y);
        }
        Vec2 operator*(const Vec2& rhs) const
        {
            return Vec2(x * rhs.x, y * rhs.y);
        }
        Vec2 operator/(const Vec2& rhs) const
        {
            return Vec2(x / rhs.x, y / rhs.y);
        }

        // Scalar arithmetic
        Vec2 operator*(float scalar) const
        {
            return Vec2(x * scalar, y * scalar);
        }
        Vec2 operator/(float scalar) const
        {
            return Vec2(x / scalar, y / scalar);
        }

        // Compound assignment
        Vec2& operator+=(const Vec2& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            return *this;
        }
        Vec2& operator-=(const Vec2& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            return *this;
        }
        Vec2& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }
        Vec2& operator/=(float scalar)
        {
            x /= scalar;
            y /= scalar;
            return *this;
        }

        // Comparison
        bool operator==(const Vec2& rhs) const
        {
            return x == rhs.x && y == rhs.y;
        }
        bool operator!=(const Vec2& rhs) const
        {
            return !(*this == rhs);
        }

        // Length and normalization
        float length() const
        {
            return std::sqrt(x * x + y * y);
        }
        float length_squared() const
        {
            return x * x + y * y;
        }

        Vec2 normalized() const
        {
            float len = length();
            return len > 0.0f ? (*this / len) : Vec2(0.0f, 0.0f);
        }

        void normalize()
        {
            float len = length();
            if (len > 0.0f)
            {
                x /= len;
                y /= len;
            }
        }

        // Dot product
        static float dot(const Vec2& a, const Vec2& b)
        {
            return a.x * b.x + a.y * b.y;
        }

        // Perpendicular (2D cross)
        static float cross(const Vec2& a, const Vec2& b)
        {
            return a.x * b.y - a.y * b.x;
        }
    };

} // namespace ZEngine::Core::Maths
