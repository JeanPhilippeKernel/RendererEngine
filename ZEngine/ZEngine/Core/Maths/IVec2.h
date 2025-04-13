#pragma once

#include <cmath>

namespace ZEngine::Core::Maths
{

    struct IVec2
    {
        int x = 0.0f;
        int y = 0.0f;

        IVec2()  = default;
        IVec2(int x, int y) : x(x), y(y) {}

        // Unary
        IVec2 operator-() const
        {
            return IVec2(-x, -y);
        }

        // Binary arithmetic
        IVec2 operator+(const IVec2& rhs) const
        {
            return IVec2(x + rhs.x, y + rhs.y);
        }
        IVec2 operator-(const IVec2& rhs) const
        {
            return IVec2(x - rhs.x, y - rhs.y);
        }
        IVec2 operator*(const IVec2& rhs) const
        {
            return IVec2(x * rhs.x, y * rhs.y);
        }
        IVec2 operator/(const IVec2& rhs) const
        {
            return IVec2(x / rhs.x, y / rhs.y);
        }

        // Scalar arithmetic
        IVec2 operator*(int scalar) const
        {
            return IVec2(x * scalar, y * scalar);
        }
        IVec2 operator/(int scalar) const
        {
            return IVec2(x / scalar, y / scalar);
        }

        // Compound assignment
        IVec2& operator+=(const IVec2& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            return *this;
        }
        IVec2& operator-=(const IVec2& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            return *this;
        }
        IVec2& operator*=(int scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }
        IVec2& operator/=(int scalar)
        {
            x /= scalar;
            y /= scalar;
            return *this;
        }

        // Comparison
        bool operator==(const IVec2& rhs) const
        {
            return x == rhs.x && y == rhs.y;
        }
        bool operator!=(const IVec2& rhs) const
        {
            return !(*this == rhs);
        }

        // Length and normalization
        int length() const
        {
            return std::sqrt(x * x + y * y);
        }
        int length_squared() const
        {
            return x * x + y * y;
        }

        IVec2 normalized() const
        {
            int len = length();
            return len > 0.0f ? (*this / len) : IVec2(0.0f, 0.0f);
        }

        void normalize()
        {
            int len = length();
            if (len > 0.0f)
            {
                x /= len;
                y /= len;
            }
        }

        // Dot product
        static int dot(const IVec2& a, const IVec2& b)
        {
            return a.x * b.x + a.y * b.y;
        }

        // Perpendicular (2D cross)
        static int cross(const IVec2& a, const IVec2& b)
        {
            return a.x * b.y - a.y * b.x;
        }
    };

} // namespace ZEngine::Core::Maths
