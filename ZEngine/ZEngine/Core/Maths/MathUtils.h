#include <Core/Maths/typedefs.h>

namespace ZEngine::Core::Maths
{
    template <>
    inline Mat2f identity<Mat2f>()
    {
        return Mat2f(1, 0, 0, 1);
    }

    template <>
    inline Mat3f identity<Mat3f>()
    {
        return Mat3f(1, 0, 0, 0, 1, 0, 0, 0, 1);
    }

    template <>
    inline Mat4f identity<Mat4f>()
    {
        return Mat4f(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    }

    // add inverse 
    // add transpose 
    // cos, sine, radian
    // min
    // clamp

} // namespace ZEngine::Core::Maths