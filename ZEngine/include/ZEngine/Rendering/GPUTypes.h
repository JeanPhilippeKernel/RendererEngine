#pragma once
#include <glm/glm.hpp>

namespace ZEngine::Rendering
{
    struct gpuvec3
    {
        float x, y, z;

        gpuvec3() = default;
        gpuvec3(float a, float b, float c) : x(a), y(b), z(c) {}
        gpuvec3(float v) : x(v), y(v), z(v) {}
        explicit gpuvec3(const glm::vec3& v) : x(v.x), y(v.y), z(v.z) {}

        gpuvec3& operator=(const glm::vec3& v)
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
        explicit gpuvec4(const glm::vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}

        gpuvec4& operator=(const glm::vec4& v)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            w = v.w;
            return *this;
        }
    };
}