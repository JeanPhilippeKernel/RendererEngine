#pragma once
#include <ZEngine/Core/Maths/Matrix.h>

namespace ZEngine::Core::Maths
{
    template <typename T>
    struct Quaternion
    {

        Quaternion(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}
        Quaternion(T x_, T y_, T z_) : x(x_), y(y_), z(z_), w(0) {}
        Quaternion() : x(0), y(0), z(0), w(1) {}
        Quaternion(const T data[]) : x(data[0]), y(data[1]), z(data[2]), w(data[3]) {}
        Quaternion(const Vec3<T> vec, T other) : x(vec.x), y(vec.y), z(vec.z), w(other) {}

        Quaternion operator-() const
        {
            return Quaternion(-x, -y, -z, -w);
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

        Quaternion& operator+=(T other)
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

        Quaternion& operator-=(T other)
        {
            x -= other;
            y -= other;
            z -= other;
            w -= other;
            return *this;
        }

        Quaternion operator*(const Quaternion& other) const
        {
            return Quaternion(*this) *= other;
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

        Quaternion& operator*=(T other)
        {
            x *= other;
            y *= other;
            z *= other;
            w *= other;
            return *this;
        }

        Quaternion operator/(const Quaternion& other) const
        {
            return *this * other.inverse();
        }
        Quaternion& operator/=(const Quaternion& other)
        {
            *this = *this * other.inverse();
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
            T len = sqrt(x * x + y * y + z * z + w * w);
            ZENGINE_VALIDATE_ASSERT(len > T(1e-8), "Cannot normalize zero or near-zero quaternion");
            return *this / len;
        }

        Quaternion conjugate() const
        {
            return {-x, -y, -z, w};
        }

        Quaternion inverse() const
        {
            T normSq = x * x + y * y + z * z + w * w;
            ZENGINE_VALIDATE_ASSERT(normSq != 0, "Cannot invert zero quaternion");
            return conjugate() / normSq;
        }

        T x, y, z, w;
    };

    template <typename T>
    inline Quaternion<T> fromEulerAngles(T pitch, T yaw, T roll)
    {
        T             halfPitch = pitch * T(0.5);
        T             halfYaw   = yaw * T(0.5);
        T             halfRoll  = roll * T(0.5);

        T             cx        = cos(halfPitch);
        T             cy        = cos(halfYaw);
        T             cz        = cos(halfRoll);

        T             sx        = sin(halfPitch);
        T             sy        = sin(halfYaw);
        T             sz        = sin(halfRoll);

        Quaternion<T> quat;
        quat.w = cx * cy * cz + sx * sy * sz;
        quat.x = sx * cy * cz - cx * sy * sz;
        quat.y = cx * sy * cz + sx * cy * sz;
        quat.z = cx * cy * sz - sx * sy * cz;

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
        T             sinHalf  = sin(angle * T(0.5));
        T             cosHalf  = cos(angle * T(0.5));

        quat.w                 = cosHalf;
        quat.x                 = normAxis.x * sinHalf;
        quat.y                 = normAxis.y * sinHalf;
        quat.z                 = normAxis.z * sinHalf;

        return quat;
    }

    template <typename T>
    inline Quaternion<T> fromRotationVector(const Vec3<T>& rotation)
    {
        T angle = rotation.magnitude();
        if (angle == T(0))
        {
            return Quaternion<T>();
        }

        Vec3<T> axis = rotation / angle;
        return fromAxisAngle(axis, angle);
    }

    template <typename T>
    inline Vec3<T> toEulerAngle(const Quaternion<T>& quat)
    {
        Vec3<T> euler;
        euler.x = atan2(2 * (quat.w * quat.x + quat.y * quat.z), 1 - 2 * (quat.x * quat.x + quat.y * quat.y));
        euler.y = asin(2 * (quat.w * quat.y - quat.z * quat.x));
        euler.z = atan2(2 * (quat.w * quat.z + quat.x * quat.y), 1 - 2 * (quat.y * quat.y + quat.z * quat.z));

        return euler;
    }

    template <typename T>
    inline void toAxisAngle(const Quaternion<T>& quat, Vec3<T>& axis, T& angle)
    {
        auto nquat    = quat.normalize();
        T    clampedW = max(T(-1), min(T(1), nquat.w));
        angle         = T(2) * acos(clampedW);

        T s           = sqrt(1 - nquat.w * nquat.w);

        if (s < T(0.0001))
        {
            axis = Vec3<T>(1, 0, 0);
        }
        else
        {
            axis = Vec3<T>(nquat.x / s, nquat.y / s, nquat.z / s);
        }
    }

    template <typename T>
    inline Vec3<T> toRotationVector(const Quaternion<T>& quat)
    {
        Vec3<T> axis;
        T       angle;
        toAxisAngle(quat, axis, angle);
        return axis * angle;
    }

    template <typename T>
    inline Quaternion<T> lerpUnclamped(const Quaternion<T>& a, const Quaternion<T>& b, T t)
    {
        auto bCopy = b;
        if (a.dot(b) < 0.0)
        {
            bCopy = -bCopy;
        }

        Quaternion<T> result = a * (1.0 - t) + bCopy * t;
        return result.normalize();
    }

    template <typename T>
    inline Quaternion<T> lerp(const Quaternion<T>& a, const Quaternion<T>& b, T t)
    {
        if (t <= 0)
            return a.normalize();
        else if (t >= 1)
            return b.normalize();
        return lerpUnclamped(a, b, t);
    }

    template <typename T>
    inline Quaternion<T> slerpUnclamped(const Quaternion<T>& a, const Quaternion<T>& b, T t)
    {
        T             dot   = a.dot(b);
        Quaternion<T> bCopy = b;

        if (dot < 0.0)
        {
            dot   = -dot;
            bCopy = -bCopy;
        }

        if (dot > 0.9995)
        {
            return lerpUnclamped(a, bCopy, t);
        }

        double        theta    = acos(dot);
        double        sinTheta = sin(theta);

        double        w1       = sin((1.0 - t) * theta) / sinTheta;
        double        w2       = sin(t * theta) / sinTheta;

        Quaternion<T> result   = a * w1 + bCopy * w2;
        return result.normalize();
    }

    template <typename T>
    inline Quaternion<T> slerp(const Quaternion<T>& a, const Quaternion<T>& b, T t)
    {
        if (t <= 0.0)
            return a.normalize();
        else if (t >= 1.0)
            return b.normalize();
        return slerpUnclamped(a, b, t);
    }

    template <typename T>
    inline Vec3<T> rotate(const Quaternion<T>& quat, const Vec3<T>& vec)
    {
        Vec3<T> qvec(quat.x, quat.y, quat.z);
        T       qw     = quat.w;

        Vec3<T> cross1 = cross3d(qvec, vec);
        Vec3<T> cross2 = cross3d(qvec, cross1 + vec * qw);

        return vec + cross2 * T(2);
    }

    template <typename T>
    Mat4<T> quaternionToMat4(const Quaternion<T>& quat)
    {
        Quaternion<T> q = quat.normalize();

        T             x = q.x, y = q.y, z = q.z, w = q.w;

        T             x2 = x * T(2);
        T             y2 = y * T(2);
        T             z2 = z * T(2);

        T             xx = x * x2;
        T             xy = x * y2;
        T             xz = x * z2;
        T             yy = y * y2;
        T             yz = y * z2;
        T             zz = z * z2;
        T             wx = w * x2;
        T             wy = w * y2;
        T             wz = w * z2;

        return Mat4<T>(T(1) - (yy + zz), xy - wz, xz + wy, T(0), xy + wz, T(1) - (xx + zz), yz - wx, T(0), xz - wy, yz + wx, T(1) - (xx + yy), T(0), T(0), T(0), T(0), T(1));
    }

    template <typename T>
    inline Vec3f QuaternionToForwardVec(const Quaternion<T>& quat)
    {
        return Vec3f(2 * ((quat.x * quat.z) + (quat.w * quat.y)), 2 * ((quat.y * quat.z) - (quat.w * quat.x)), 1 - 2 * ((quat.x * quat.x) + (quat.y * quat.y)));
    }

    template <typename T>
    inline Vec3f QuaternionToRightVec(const Quaternion<T>& quat)
    {
        return Vec3f(1 - 2 * ((quat.y * quat.y) + (quat.z * quat.z)), 2 * ((quat.x * quat.y) + (quat.w * quat.z)), 2 * ((quat.x * quat.z) - (quat.w * quat.y)));
    }

    template <typename T>
    inline Vec3f QuaternionToUpVec(const Quaternion<T>& quat)
    {
        return Vec3f(2 * ((quat.x * quat.y) - (quat.w * quat.z)), 1 - 2 * ((quat.x * quat.x) + (quat.z * quat.z)), 2 * ((quat.y * quat.z) + (quat.w * quat.x)));
    }

    using Quatf = Quaternion<float>;

} // namespace ZEngine::Core::Maths
