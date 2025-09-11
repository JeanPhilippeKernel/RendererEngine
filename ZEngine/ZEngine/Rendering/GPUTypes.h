#pragma once
#include <Core/Maths/Vec.h>

namespace ZEngine::Rendering
{
    struct gpuvec3
    {
        float x, y, z;

        gpuvec3() = default;
        gpuvec3(float a, float b, float c) : x(a), y(b), z(c) {}
        gpuvec3(float v) : x(v), y(v), z(v) {}
        explicit gpuvec3(const ZEngine::Core::Maths::Vec3f& v) : x(v.x), y(v.y), z(v.z) {}

        template <typename T>
        T As()
        {
            if constexpr (std::is_same_v<T, ZEngine::Core::Maths::Vec2f>)
            {
                return T(x, y);
            }
            else
            {
                return T(x, y, z);
            }
        }

        gpuvec3& operator=(const ZEngine::Core::Maths::Vec3f& v)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            return *this;
        }
    };

    struct gpuvec4
    {
        float x, y, z, w;

        gpuvec4() = default;
        gpuvec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
        gpuvec4(float v) : x(v), y(v), z(v), w(v) {}
        explicit gpuvec4(const ZEngine::Core::Maths::Vec4f& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}

        template <typename T>
        T As()
        {
            if constexpr (std::is_same_v<T, ZEngine::Core::Maths::Vec3f>)
            {
                return T(x, y, z);
            }
            else if constexpr (std::is_same_v<T, ZEngine::Core::Maths::Vec2f>)
            {
                return T(x, y);
            }
            else
            {
                return T(x, y, z, w);
            }
        }

        gpuvec4& operator=(const ZEngine::Core::Maths::Vec4f& v)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            w = v.w;
            return *this;
        }

        gpuvec4& operator=(const gpuvec4& v)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            w = v.w;
            return *this;
        }

        gpuvec4& operator=(const float v[4])
        {
            x = v[0];
            y = v[1];
            z = v[2];
            w = v[3];
            return *this;
        }
    };
} // namespace ZEngine::Rendering