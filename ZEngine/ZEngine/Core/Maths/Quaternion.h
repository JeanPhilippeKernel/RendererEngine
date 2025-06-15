#include <Vec.h>

namespace ZEngine::Core::Maths
{
    template <typename T>
    struct Quaternion
    {

        Quaternion(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}
        Quaternion(T x_, T y_, T z_) : x(x_), y(y_), z(z_), w(0) {}
        Quaternion() : x(0), y(0), z(0), w(1) {}
        Quaternion(const T data[]) : x(data[0]), y(data[1]), z(data[2]), w(data[3]) {}
        Quaternion(const Vec3<T> vec, double other) : x(vec.x), y(vec.y), z(vec.z), w(other) {}

        Quaternion Identity()
        {
            return Quaternion(0, 0, 0, 1); // same as default tho so no need for identity or should i remove default ?
        }

        Quaternion operator+(const Quaternion& other) const
        {
            return Quaternion(x + other.x, y + other.y, z + other.z, w + other.w);
        }
        Quaternion& operator+=(const Quaternion& other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            w += other.w;
            return *this;
        }
        Quaternion operator+(T other) const
        {
            return Quaternion(x + other, y + other, z + other, w + other);
        }

        Quaternion operator+=(T other)
        {
            x += other;
            y += other;
            z += other;
            w += other;
            return *this;
        }

        Quaternion operator-(const Quaternion& other) const
        {
            return Quaternion(x - other.x, y - other.y, z - other.z, w - other.w);
        }
        Quaternion& operator-=(const Quaternion& other)
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            w -= other.w;
            return *this;
        }
        Quaternion operator-(T other) const
        {
            return Quaternion(x - other, y - other, z - other, w - other);
        }

        Quaternion operator-=(T other)
        {
            x -= other;
            y -= other;
            z -= other;
            w -= other;
            return *this;
        }

        Quaternion operator*(const Quaternion& other) const
        {
            return *this *= other;
        }

        Quaternion& operator*=(const Quaternion& other)
        {
            T newX = w * other.x + x * other.w + y * other.z - z * other.y;
            T newY = w * other.y - x * other.z + y * other.w + z * other.x;
            T newZ = w * other.z + x * other.y - y * other.x + z * other.w;
            T newW = w * other.w - x * other.x - y * other.y - z * other.z;

            x      = newX;
            y      = newY;
            z      = newZ;
            w      = newW;
            return *this;
        }

        Quaternion operator*(T other) const
        {
            return Quaternion(x * other, y * other, z * other, w * other);
        }

        Quaternion operator*=(T other)
        {
            x *= other;
            y *= other;
            z *= other;
            w *= other;
            return *this;
        }

        Quaternion operator/(const Quaternion& other) const
        {
            return Quaternion(x / other.x, y / other.y, z / other.z, w / other.w);
        }
        Quaternion& operator/=(const Quaternion& other)
        {
            x /= other.x;
            y /= other.y;
            z /= other.z;
            w /= other.w;
            return *this;
        }
        Quaternion operator/(T other) const
        {
            return Quaternion(x / other, y / other, z / other, w / other);
        }

        Quaternion operator/=(T other)
        {
            x /= other;
            y /= other;
            z /= other;
            w /= other;
            return *this;
        }

        T dot(const Quaternion& other) const
        {
            return x * other.x + y * other.y + z * other.z + w * other.w;
        }

        Quaternion normalize() const
        {
            T len = std::sqrt(x * x + y * y + z * z + w * w);
            ZENGINE_VALIDATE_ASSERT(len != 0, "Cannot normalize zero vector");
            return *this / len;
        }

        T x, y, z, w;
    };

    template <typename T>
    inline Quaternion<T> fromEulerAngles(double a, double b, double c)
    {
        double        xs = std::sin(a * 0.5);
        double        ys = std::sin(b * 0.5);
        double        zs = std::sin(c * 0.5);
        double        xc = std::cos(a * 0.5);
        double        yc = std::cos(b * 0.5);
        double        zc = std::cos(c * 0.5);

        Quaternion<T> quat;
        quat.x = xs * yc * zc - xc * ys * xs;
        quat.y = xc * zc * ys - yc * xs * zs;
        quat.z = xc * yc * zs - zc * xs * ys;
        quat.w = xs * ys * zs + xc * yc * zc;

        return quat;
    }

    template <typename T>
    inline Quaternion<T> fromEulerAngles(const Vec3<T>& rotation)
    {
        return fromEulerAngles(rotation.x, rotation.y, rotation.z);
    }

    template <typename T>
    inline Quaternion<T> fromAxisAngle(const Vec3<T>& axis, T angle)
    {
        Quaternion<T> quat;

        Vec3<T>       normAxis = axis.normalize();
        T             sinHalf  = std::sin(angle * T(0.5));
        T             cosHalf  = std::cos(angle * T(0.5));

        quat.w                 = cosHalf;
        quat.x                 = normAxis.x * sinHalf;
        quat.y                 = normAxis.y * sinHalf;
        quat.z                 = normAxis.z * sinHalf;

        return quat;
    }

    template <typename T>
    inline Vec3<T> toEulerAngle(const Quaternion<T>& quat)
    {
    }
} // namespace ZEngine::Core::Maths