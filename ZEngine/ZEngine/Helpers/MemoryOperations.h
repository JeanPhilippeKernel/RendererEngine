#pragma once
#include <ZEngineDef.h>
#include <cstring>

#ifdef __STDC_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#include <string.h>
#endif

namespace ZEngine::Helpers
{
    constexpr int MEMORY_OP_SUCCESS = 0;
    constexpr int MEMORY_OP_FAILURE = -1;

#if defined(__STDC_LIB_EXT1__) && defined(__STDC_WANT_LIB_EXT1__) && (__STDC_WANT_LIB_EXT1__ >= 1) || (defined(__STDC_WANT_SECURE_LIB__) && __STDC_WANT_SECURE_LIB__ >= 1)
#define SECURE_C11_FUNCTIONS_AVAILABLE 1
#else
#define SECURE_C11_FUNCTIONS_AVAILABLE 0
#endif

    inline int secure_memset(void* destination, int value, size_t count, size_t destinationSize)
    {
        if (!destination)
        {
            return MEMORY_OP_FAILURE;
        }

        if (count > destinationSize)
        {
            return MEMORY_OP_FAILURE;
        }

        return (std::memset(destination, value, count) == destination) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
    }

    inline int secure_memcpy(void* dest, size_t destSize, const void* src, size_t count)
    {
        if (!dest || !src)
        {
            return MEMORY_OP_FAILURE;
        }

        if (count > destSize)
        {
            return MEMORY_OP_FAILURE;
        }

#if SECURE_C11_FUNCTIONS_AVAILABLE
        errno_t err = memcpy_s(dest, destSize, src, count);
        return (err == 0) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
#else
        return (std::memcpy(dest, src, count) == dest) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;

#endif
    }

    inline int secure_memmove(void* dest, size_t destSize, const void* src, size_t count)
    {
        if (!dest || !src)
        {
            return MEMORY_OP_FAILURE;
        }

        if (count > destSize)
        {
            return MEMORY_OP_FAILURE;
        }

#if SECURE_C11_FUNCTIONS_AVAILABLE
        errno_t err = memmove_s(dest, destSize, src, count);
        return (err == 0) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
#else
        return (std::memmove(dest, src, count) == dest) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
#endif
    }

    inline int secure_strncpy(char* dest, size_t destSize, const char* src, size_t count)
    {
        if (!dest || !src)
        {
            return MEMORY_OP_FAILURE;
        }

        if (destSize == 0 || count >= destSize)
        {
            return MEMORY_OP_FAILURE;
        }

#if SECURE_C11_FUNCTIONS_AVAILABLE
        errno_t err = strncpy_s(dest, destSize, src, count);
        return (err == 0) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
#else
        return (std::strncpy(dest, src, count) == dest) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
#endif
    }
    inline size_t secure_strlen(const char* str)
    {
        if (!str)
        {
            return 0;
        }
        return std::strlen(str);
    }

    inline int secure_strcpy(char* dest, size_t destSize, const char* src)
    {
        if (!dest || !src)
        {
            return MEMORY_OP_FAILURE;
        }

        size_t srcLength = secure_strlen(src);
        if (srcLength + 1 > destSize)
        {
            return MEMORY_OP_FAILURE;
        }

#if SECURE_C11_FUNCTIONS_AVAILABLE
        errno_t err = strcpy_s(dest, destSize, src);
        return (err == 0) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
#else
        return (std::strcpy(dest, src) == dest) ? MEMORY_OP_SUCCESS : MEMORY_OP_FAILURE;
#endif
    }

    inline int secure_strcmp(const char* str1, const char* str2)
    {
        if (!str1 || !str2)
        {
            return MEMORY_OP_FAILURE;
        }

#if SECURE_C11_FUNCTIONS_AVAILABLE
        // There is no secure C11 version of strcmp, so we fall back to manual comparison
#endif

        while (*str1 && (*str1 == *str2))
        {
            ++str1;
            ++str2;
        }

        return static_cast<unsigned char>(*str1) - static_cast<unsigned char>(*str2);
    }

    inline int secure_memcmp(const void* ptr1, size_t ptr1Size, const void* ptr2, size_t ptr2Size, size_t num)
    {
        if (!ptr1 || !ptr2)
        {
            return MEMORY_OP_FAILURE;
        }

        if (num > ptr1Size || num > ptr2Size)
        {
            return MEMORY_OP_FAILURE;
        }

        return std::memcmp(ptr1, ptr2, num);
    }

    inline bool is_power_of_two(uintptr_t x)
    {
        return (x & (x - 1)) == 0;
    }

    inline uintptr_t memory_align(uintptr_t ptr, size_t align)
    {
        uintptr_t p, a, mod;

        ZENGINE_VALIDATE_ASSERT(is_power_of_two(align), "Alignment should be power of two");

        p   = ptr;
        a   = static_cast<uintptr_t>(align);
        mod = p & (a - 1);
        if (mod != 0)
        {
            p += a - mod;
        }
        return p;
    }

    inline size_t memory_align_size_t(size_t ptr, size_t align)
    {
        size_t p, a, mod;

        ZENGINE_VALIDATE_ASSERT(is_power_of_two((uintptr_t) align), "Alignment should be power of two");

        p   = ptr;
        a   = align;
        mod = p & (a - 1);
        if (mod != 0)
        {
            p += a - mod;
        }
        return p;
    }

} // namespace ZEngine::Helpers
