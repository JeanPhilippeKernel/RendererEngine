#pragma once
#include <Allocator.h>
#include <cmath>

using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Maths
{
    template <typename T, size_t N>
    class Vector
    {
    public:
        void init(ArenaAllocator* allocator)
        {
            m_allocator = allocator;
            m_data      = static_cast<T*>(ZAlloc(m_allocator, sizeof(T) * N, ZAlignof(T)));
        }

        void init(ArenaAllocator* allocator, T x, T y)
        {
            ZENGINE_VALIDATE_ASSERT(N == 2, "This init overload is only valid for 2D vectors.");
            init(allocator);
            m_data[0] = x;
            m_data[1] = y;
        }

        void init(ArenaAllocator* allocator, T x, T y, T z)
        {
            ZENGINE_VALIDATE_ASSERT(N == 3, "This init overload is only valid for 3D vectors.");
            init(allocator);
            m_data[0] = x;
            m_data[1] = y;
            m_data[2] = z;
        }

        void init(ArenaAllocator* allocator, T x, T y, T z, T w)
        {
            ZENGINE_VALIDATE_ASSERT(N == 4, "This init overload is only valid for 4D vectors.");
            init(allocator);
            m_data[0] = x;
            m_data[1] = y;
            m_data[2] = z;
            m_data[3] = w;
        }
        T& x()
        {
            return m_data[0];
        }
        T& y()
        {
            return m_data[1];
        }
        T& z()
        {
            ZENGINE_VALIDATE_ASSERT(N > 2, "Z is only valid for 3D vectors.");
            return m_data[2];
        }
        T& w()
        {
            ZENGINE_VALIDATE_ASSERT(N > 3, "W is only valid for 4D vectors.");
            return m_data[3];
        }

        T& operator[](size_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < N, "Index out of range");
            return m_data[index];
        }

        const T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < N, "Index out of range");
            return m_data[index];
        }

        size_t size() const
        {
            return N;
        }

        static bool IsEqual(T a, T b)
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                constexpr T epsilon = static_cast<T>(1e-6);
                return std::fabs(a - b) < epsilon;
            }
            else
            {
                return a == b;
            }
        }

        bool operator==(const Vector<T, N>& other) const
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (!IsEqual(m_data[i], other.m_data[i]))
                    return false;
            }
            return true;
        }

        bool operator!=(const Vector<T, N>& other) const
        {
            return !(*this == other);
        }

        bool operator>(const Vector<T, N>& other) const
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (m_data[i] > other.m_data[i])
                    return true;
                if (m_data[i] < other.m_data[i])
                    return false;
            }
            return false;
        }

        bool operator<(const Vector<T, N>& other) const
        {
            for (size_t i = 0; i < N; ++i)
            {
                if (m_data[i] < other.m_data[i])
                    return true;
                if (m_data[i] > other.m_data[i])
                    return false;
            }
            return false;
        }

        Vector<T, N>& operator+=(const Vector<T, N>& other)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] += other[i];
            }
            return *this;
        }

        Vector<T, N>& operator-=(const Vector<T, N>& other)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] -= other[i];
            }
            return *this;
        }

        Vector<T, N> operator*(T scalar) const
        {
            Vector<T, N> result;
            result.init(m_allocator);
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] * scalar;
            }
            return result;
        }

        Vector<T, N> operator/(T scalar) const
        {
            Vector<T, N> result;
            result.init(m_allocator);
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] / scalar;
            }
            return result;
        }

        Vector<T, N>& operator*=(T scalar)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] *= scalar;
            }
            return *this;
        }

        Vector<T, N>& operator/=(T scalar)
        {
            for (size_t i = 0; i < N; ++i)
            {
                m_data[i] /= scalar;
            }
            return *this;
        }
        Vector<T, N> operator+(const Vector<T, N>& other) const
        {
            Vector<T, N> result;
            result.init(m_allocator);
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] + other[i];
            }
            return result;
        }

        Vector<T, N> operator-(const Vector<T, N>& other) const
        {
            Vector<T, N> result;
            result.init(m_allocator);
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] - other[i];
            }
            return result;
        }

        Vector<T, N> operator*(const Vector<T, N>& other) const
        {
            Vector<T, N> result;
            result.init(m_allocator);
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] * other[i];
            }
            return result;
        }

        T dot(const Vector<T, N>& other) const
        {
            T result = T();
            for (size_t i = 0; i < N; ++i)
            {
                result += m_data[i] * other[i];
            }
            return result;
        }

        T magnitude() const
        {
            return std::sqrt(dot(*this));
        }

        Vector<T, N> normalized() const
        {
            Vector<T, N> result;
            result.init(m_allocator);
            T len = magnitude();
            for (size_t i = 0; i < N; ++i)
            {
                result[i] = m_data[i] / len;
            }
            return result;
        }

        T cross2d(const Vector<T, N>& other) const
        {
            ZENGINE_VALIDATE_ASSERT(N == 2, "cross2d() is only defined for 2D vectors.");
            return (m_data[0] * other[1]) - (m_data[1] * other[0]);
        }

        Vector<T, N> cross3d(const Vector<T, N>& other) const
        {
            ZENGINE_VALIDATE_ASSERT(N == 3, "cross3d() is only defined for 3D vectors.");
            Vector<T, N> result;
            result.init(m_allocator);

            result[0] = m_data[1] * other[2] - m_data[2] * other[1];
            result[1] = m_data[2] * other[0] - m_data[0] * other[2];
            result[2] = m_data[0] * other[1] - m_data[1] * other[0];
            return result;
        }

        T* data()
        {
            return m_data;
        }
        const T* data() const
        {
            return m_data;
        }

    private:
        ArenaAllocator* m_allocator = nullptr;
        T*              m_data      = nullptr;
    };
} // namespace ZEngine::Core::Maths
