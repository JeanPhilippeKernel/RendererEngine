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
    };
} // namespace ZEngine::Core::Maths