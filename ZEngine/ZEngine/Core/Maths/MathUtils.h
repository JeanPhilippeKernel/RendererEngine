#pragma once
namespace ZEngine::Core::Maths
{
    template <typename T>
    constexpr T DEG_TO_RAD = T(0.017453292519943295769236907684886127);

    template <typename T>
    constexpr T RAD_TO_DEG = T(57.295779513082320876798154814105170332);

    template <typename T>
    constexpr T PI = T(3.141592653589793238462643383279502884);

    template <typename T>
    constexpr T TWO_PI = T(6.283185307179586476925286766559005768);

    template <typename T>
    constexpr T HALF_PI = T(1.570796326794896619231321691639751442);

    template <typename T>
    constexpr T clamp(T value, T minVal, T maxVal)
    {
        return (value < minVal) ? minVal : (value > maxVal) ? maxVal : value;
    }

    template <typename T>
    constexpr T min(T a, T b)
    {
        return (a < b) ? a : b;
    }

    template <typename T>
    constexpr T max(T a, T b)
    {
        return (a > b) ? a : b;
    }

    template <typename T>
    constexpr T abs(T value)
    {
        return (value < T(0)) ? -value : value;
    }

    template <typename T>
    constexpr T radians(T degrees)
    {
        return degrees * DEG_TO_RAD<T>;
    }

    template <typename T>
    constexpr T degrees(T radians)
    {
        return radians * RAD_TO_DEG<T>;
    }

    template <typename T>
    T sqrt(T x)
    {
        if (x <= T(0))
            return T(0);
        constexpr T epsilon = (sizeof(T) == sizeof(float)) ? T(1e-7) : T(1e-15);

        T           guess   = x;
        T           prev    = T(0);
        while (abs(guess - prev) > epsilon)
        {
            prev  = guess;
            guess = (guess + x / guess) * T(0.5);
        }

        return guess;
    }

    template <typename T>
    struct SinCoefficients
    {
        static constexpr T c0 = T(0.99999999999999999999);
        static constexpr T c1 = T(-0.16666666666666666666);
        static constexpr T c2 = T(0.00833333333333333333);
        static constexpr T c3 = T(-0.00019841269841269841);
        static constexpr T c4 = T(0.00000275573192239858);
        static constexpr T c5 = T(-0.00000002505210838544);
        static constexpr T c6 = T(0.00000000016059043837);
    };

    template <typename T>
    struct CosCoefficients
    {
        static constexpr T c0 = T(1.0);
        static constexpr T c1 = T(-0.5);
        static constexpr T c2 = T(0.04166666666666666666);
        static constexpr T c3 = T(-0.00138888888888888888);
        static constexpr T c4 = T(0.00002480158730158730);
        static constexpr T c5 = T(-0.00000027557319223985);
        static constexpr T c6 = T(0.00000000208767569878);
    };

    template <typename T>
    struct AtanCoefficients
    {
        static constexpr T c0 = T(0.99999999999999999999);
        static constexpr T c1 = T(-0.33333333333333333333);
        static constexpr T c2 = T(0.20000000000000000000);
        static constexpr T c3 = T(-0.14285714285714285714);
        static constexpr T c4 = T(0.11111111111111111111);
        static constexpr T c5 = T(-0.09090909090909090909);
        static constexpr T c6 = T(0.07692307692307692307);
    };

    template <typename T>
    constexpr T estrin_poly_7(T x, T c0, T c1, T c2, T c3, T c4, T c5, T c6)
    {
        T x2    = x * x;
        T x4    = x2 * x2;

        T p01   = c0 + c1 * x;
        T p23   = c2 + c3 * x;
        T p45   = c4 + c5 * x;
        T p6    = c6;

        T p0123 = p01 + p23 * x2;
        T p456  = p45 + p6 * x;

        return p0123 + p456 * x4;
    }

    template <typename T>
    T sin(T x)
    {
        while (x > PI<T>)
            x -= TWO_PI<T>;
        while (x < -PI<T>)
            x += TWO_PI<T>;

        bool negate = false;
        if (x > HALF_PI<T>)
        {
            x = PI<T> - x;
        }
        else if (x < -HALF_PI<T>)
        {
            x      = -PI<T> - x;
            negate = true;
        }
        else if (x < T(0))
        {
            x      = -x;
            negate = true;
        }

        using Coeff = SinCoefficients<T>;
        T x2        = x * x;
        T result    = x * estrin_poly_7(x2, Coeff::c0, Coeff::c1, Coeff::c2, Coeff::c3, Coeff::c4, Coeff::c5, Coeff::c6);

        return negate ? -result : result;
    }

    template <typename T>
    T cos(T x)
    {
        while (x > PI<T>)
            x -= TWO_PI<T>;
        while (x < -PI<T>)
            x += TWO_PI<T>;

        x           = abs(x);

        bool negate = false;
        if (x > HALF_PI<T>)
        {
            x      = PI<T> - x;
            negate = true;
        }

        using Coeff = CosCoefficients<T>;
        T x2        = x * x;
        T result    = estrin_poly_7(x2, Coeff::c0, Coeff::c1, Coeff::c2, Coeff::c3, Coeff::c4, Coeff::c5, Coeff::c6);

        return negate ? -result : result;
    }

    template <typename T>
    T atan(T x)
    {
        bool negate     = false;
        bool reciprocal = false;

        if (x < T(0))
        {
            x      = -x;
            negate = true;
        }

        if (x > T(1))
        {
            x          = T(1) / x;
            reciprocal = true;
        }

        using Coeff = AtanCoefficients<T>;
        T x2        = x * x;
        T result    = x * estrin_poly_7(x2, Coeff::c0, Coeff::c1, Coeff::c2, Coeff::c3, Coeff::c4, Coeff::c5, Coeff::c6);

        if (reciprocal)
            result = HALF_PI<T> - result;

        return negate ? -result : result;
    }

    template <typename T>
    T atan2(T y, T x)
    {
        if (x > T(0))
            return atan(y / x);
        else if (x < T(0))
        {
            if (y >= T(0))
                return atan(y / x) + PI<T>;
            else
                return atan(y / x) - PI<T>;
        }
        else
        {
            if (y > T(0))
                return HALF_PI<T>;
            else if (y < T(0))
                return -HALF_PI<T>;
            else
                return T(0);
        }
    }

    template <typename T>
    T acos(T x)
    {
        if (abs(x) > T(1))
            return T(0);

        if (x == T(0))
            return HALF_PI<T>;

        T sqrt_term = sqrt(T(1) - x * x);
        return atan2(sqrt_term, x);
    }

} // namespace ZEngine::Core::Maths