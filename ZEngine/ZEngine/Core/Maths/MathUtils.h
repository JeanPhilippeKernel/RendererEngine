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
    T fabs(T value)
    {
        return (value < T(0)) ? -value : value;
    }

    template <typename T>
    constexpr T radians(T degrees)
    {
        return degrees * DEG_TO_RAD<T>;
    }

    template <typename T>
    constexpr T abs(T value)
    {
        return (value < T(0)) ? -value : value;
    }

    template <typename T>
    T sqrt(T x)
    {
        if (x <= T(0))
            return T(0);

        T guess = x;
        T prev  = T(0);

        while (abs(guess - prev) > T(1e-10))
        {
            prev  = guess;
            guess = (guess + x / guess) * T(0.5);
        }

        return guess;
    }

    template <typename T>
    T sin(T x)
    {
        while (x > PI<T>)
            x -= TWO_PI<T>;
        while (x < -PI<T>)
            x += TWO_PI<T>;

        T result    = x;
        T term      = x;
        T x_squared = x * x;

        for (int i = 1; i <= 10; ++i)
        {
            term   *= -x_squared / ((2 * i) * (2 * i + 1));
            result += term;
        }

        return result;
    }

    template <typename T>
    T cos(T x)
    {
        return sin(HALF_PI<T> - x);
    }

    template <typename T>
    T atan(T x)
    {
        if (abs(x) > T(1))
        {
            T result = HALF_PI<T> - atan(T(1) / x);
            return (x < T(0)) ? -result : result;
        }

        T result    = x;
        T term      = x;
        T x_squared = x * x;

        for (int i = 1; i <= 15; ++i)
        {
            term   *= -x_squared;
            result += term / (2 * i + 1);
        }

        return result;
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