#pragma once
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <ZEngine/Logging/LoggerDefinition.h>

#define BIT(x)                 (1 << (x))
#define ZENGINE_EXIT_FAILURE() exit(EXIT_FAILURE);

#define ZENGINE_KEYCODE        ZEngine::Windows::Inputs::GlfwKeyCode

#ifdef _MSC_VER
#define ZENGINE_DEBUG_BREAK() \
    __debugbreak();           \
    __assume(false);
#elif defined(__APPLE__)
#include <signal.h>
#define ZENGINE_DEBUG_BREAK() __builtin_trap();
#else
#include <signal.h>
#define ZENGINE_DEBUG_BREAK() \
    raise(SIGTRAP);           \
    __builtin_unreachable();
#endif

#ifdef _WIN32
#define PLATFORM_OS_BACKSLASH '\\'
#elif defined(__APPLE__) || defined(__linux__)
#define PLATFORM_OS_BACKSLASH '/'
#else
#define PLATFORM_OS_BACKSLASH
#endif

#if defined(NDEBUG) || defined(ZENGINE_RELWITHDEBINFO) || defined(ZENGINE_RELEASE)
#define ZENGINE_VALIDATE_ASSERT(cond, msg)                                                         \
    do                                                                                             \
    {                                                                                              \
        if (!(cond)) [[unlikely]]                                                                  \
        {                                                                                          \
            ::ZEngine::CrashHandlers::CrashHandler::OnAssertionFailure(__FILE__, __LINE__, (msg)); \
        }                                                                                          \
    } while (false);
#else
// Debug: use debugger break, no crash handler.
#define ZENGINE_VALIDATE_ASSERT(cond, msg) \
    do                                     \
    {                                      \
        if (!(cond))                       \
        {                                  \
            ZENGINE_CORE_CRITICAL(msg)     \
            ZENGINE_DEBUG_BREAK()          \
        }                                  \
    } while (false);
#endif

#define ZENGINE_DESTROY_VULKAN_HANDLE(device, function, handle, ...) \
    if (device && handle)                                            \
    {                                                                \
        function(device, handle, __VA_ARGS__);                       \
        handle = nullptr;                                            \
    }

#define ZENGINE_CLEAR_STD_VECTOR(collection) \
    if (!collection.empty())                 \
    {                                        \
        collection.clear();                  \
        collection.shrink_to_fit();          \
    }

#define SINGLE_ARG(...)     __VA_ARGS__

#define MAX_FILE_PATH_COUNT 256
#define DEFAULT_STR_BUFFER  256

#define ZRawPtr(X)          X*
#define ZDEFINE_PTR(X)      typedef ZRawPtr(X) X##Ptr

#define CHECK_AND_ESCAPE_NULL(handle) \
    if (!handle)                      \
    {                                 \
        return;                       \
    }

/*
 * Allocator and Memory Macros
 */
#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGNMENT (2 * sizeof(void*))
#endif // !DEFAULT_ALIGNMENT

#define ZKilo(size)                    (size * 1024)
#define ZMega(size)                    (ZKilo(size) * 1024)
#define ZGiga(size)                    (ZMega(size) * 1024)

#define ZPush(allocator, type, size)   ((type*) (allocator)->Allocate(size, DEFAULT_ALIGNMENT, __FILE__, __LINE__))

#define ZPushArray(arena, type, count) ZPush(arena, type, (sizeof(type) * count))
#define ZPushString(arena, count)      ZPushArray(arena, char, count)
#define ZPushStruct(arena, type)       ZPushArray(arena, type, 1)

#ifdef __cplusplus
#define ZPushStructCtor(arena, type)          (new (ZPushStruct(arena, type)) type())
#define ZPushStructCtorArgs(arena, type, ...) (new (ZPushStruct(arena, type)) type(__VA_ARGS__))
#define ZConstruct(ptr, type)                 (new (ptr) type())
#define ZConstructArgs(ptr, type, ...)        (new (ptr) type(__VA_ARGS__))
#endif

#define ZPushDynamicArray(pool, type)                          ((type*) (pool)->Allocate(__FILE__, __LINE__))
#define ZAlloc(allocator, size, alignment)                     ((allocator)->Allocate((size), (alignment)))
#define ZResize(allocator, ptr, old_size, new_size, alignment) ((allocator)->Resize((ptr), (old_size), (new_size), (alignment)))
#define ZAlignof(type)                                         ((alignof(type) < DEFAULT_ALIGNMENT) ? DEFAULT_ALIGNMENT : alignof(type))

/*
 *
 */
#define MAKE_MAGIC(a, b, c, d)                                 ((uint32_t) (a) << 24 | (uint32_t) (b) << 16 | (uint32_t) (c) << 8 | (uint32_t) (d))
#define MAKE_VERSION(major, minor, patch)                      (((uint32_t) (major) << 16) | ((uint32_t) (minor) << 8) | ((uint32_t) (patch)))

#define ZEASSET_MAGIC                                          MAKE_MAGIC('Z', 'A', 'S', 'T')
#define ZEMESH_MAGIC                                           MAKE_MAGIC('Z', 'M', 'S', 'H')
#define ZEMATERIAL_MAGIC                                       MAKE_MAGIC('Z', 'M', 'A', 'T')
#define ZETEXTURES_MAGIC                                       MAKE_MAGIC('Z', 'T', 'E', 'X')
#define ZESCENE_MAGIC                                          MAKE_MAGIC('Z', 'S', 'C', 'N')
#define ZENVMAP_MAGIC                                          MAKE_MAGIC('Z', 'E', 'N', 'V')
#define ASSET_FILE_VERSION                                     MAKE_VERSION(1, 0, 0)
#define SCENE_FILE_VERSION                                     MAKE_VERSION(2, 0, 0)

typedef const char* cstring;

#ifdef __cpp_lib_hardware_interference_size
#include <atomic>
#include <new>
constexpr auto CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
#include <atomic>
constexpr auto CACHE_LINE_SIZE = 64;
#endif

template <typename T>
struct alignas(CACHE_LINE_SIZE) PaddedAtomic
{
    std::atomic<T> value = {};
};
