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
    constexpr T floor(T x)
    {
        T intPart = static_cast<T>(static_cast<long long>(x));
        return (x < T(0) && x != intPart) ? intPart - T(1) : intPart;
    }

    template <typename T>
    T sqrt(T x)
    {
        if (x <= T(0))
        {
            return T(0);
        }
        constexpr T epsilon       = (sizeof(T) == sizeof(float)) ? T(1e-7) : T(1e-15);
        T           guess         = x;
        T           prev          = T(0);
        int         iteration     = 0;
        const int   max_iteration = 20;
        while (abs(guess - prev) > epsilon && iteration < max_iteration)
        {
            prev  = guess;
            guess = (guess + x / guess) * T(0.5);
            ++iteration;
        }
        return guess;
    }

    template <typename T>
    T sin(T x)
    {
        x      -= TWO_PI<T> * floor(x / TWO_PI<T>);

        T sign  = T(1);
        if (x > HALF_PI<T>)
        {
            x    = PI<T> - x;
            sign = -1;
        }
        else if (x < -HALF_PI<T>)
        {
            x    = -PI<T> - x;
            sign = -1;
        }

        T       x2 = x * x;

        const T c1 = T(-0.16666667);
        const T c2 = T(0.0083333310);
        const T c3 = T(-0.00019840874);

        T       t1 = c2 + c3 * x2;
        T       t2 = c1 + t1 * x2;

        return sign * x * (1 + t2 * x2);
    }

    template <typename T>
    T cos(T x)
    {
        return sin<T>(HALF_PI<T> - x);
    }

    template <typename T>
    T atan(T x)
    {
        if (abs(x) > T(1))
        {
            T result = (x > T(0) ? HALF_PI<T> : -HALF_PI<T>) -atan(T(1) / x);
            return result;
        }

        T       x2 = x * x;

        const T c1 = T(-0.33333333);
        const T c2 = T(0.2);
        const T c3 = T(-0.14285714);
        const T c4 = T(0.11111111);

        T       t1 = c3 + c4 * x2;
        T       t2 = c2 + t1 * x2;
        T       t3 = c1 + t2 * x2;

        return x + x * x2 * t3;
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

    template <typename T>
    constexpr bool epsilonNotEqual(T a, T b, T epsilon = T(1e-6))
    {
        return abs(a - b) > epsilon;
    }

    template <typename T>
    constexpr bool epsilonEqual(T a, T b, T epsilon = T(1e-6))
    {
        return abs(a - b) == epsilon;
    }

    template <typename T>
    T asin(T x)
    {
        if (abs(x) > T(1))
            return T(0);

        if (x == T(1))
            return HALF_PI<T>;
        else if (x == T(-1))
            return -HALF_PI<T>;
        else if (x == T(0))
            return T(0);

        T sqrt_term = sqrt(T(1) - x * x);
        return atan2(x, sqrt_term);
    }

    template <typename T>
    T tan(T x)
    {
        T c = cos<T>(x);
        if (c == T(0))
        {
            return (x > T(0)) ? T(1e30) : T(-1e30);
        }
        return sin<T>(x) / c;
    }

} // namespace ZEngine::Core::Maths