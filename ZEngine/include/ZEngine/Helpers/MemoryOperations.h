#pragma once
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

} // namespace ZEngine::Helpers
