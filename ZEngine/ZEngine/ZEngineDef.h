#pragma once
#include <ZEngine/CrashHandlers/CrashHandler.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Windows/Inputs/KeyCode.h>
#include <source_location>

inline constexpr auto BIT(auto x)
{
    return 1 << x;
}

inline void ZENGINE_EXIT_FAILURE()
{
    exit(EXIT_FAILURE);
}

using ZENGINE_KEYCODE = ZEngine::Windows::Inputs::GlfwKeyCode;

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

#define SINGLE_ARG(...) __VA_ARGS__

inline constexpr auto MAX_FILE_PATH_COUNT{256};
inline constexpr auto DEFAULT_STR_BUFFER{256};

#define ZDEFINE_PTR(X) typedef X* X##Ptr

#define CHECK_AND_ESCAPE_NULL(handle) \
    if (!handle)                      \
    {                                 \
        return;                       \
    }

inline constexpr auto     DEFAULT_ALIGNMENT{2 * sizeof(void*)};

inline constexpr uint64_t ZKilo(std::integral auto size)
{
    return static_cast<uint64_t>(size) * 1024ULL;
}

inline constexpr uint64_t ZMega(std::integral auto size)
{
    return static_cast<uint64_t>(size) * 1024ULL * 1024ULL;
}

inline constexpr uint64_t ZGiga(std::integral auto size)
{
    return static_cast<uint64_t>(size) * 1024ULL * 1024ULL * 1024ULL;
}

// #define ZPush(allocator, type, size) ((type*) (allocator)->Allocate(size, DEFAULT_ALIGNMENT, __FILE__, __LINE__))
template <typename Type, typename Allocator>
inline void* ZPush(Allocator* allocator, std::size_t size, std::source_location location = std::source_location::current())
{
    return allocator->Allocate(size, DEFAULT_ALIGNMENT, location.file_name(), location.line());
}

// #define ZPushArray(arena, type, count) ZPush(arena, type, (sizeof(type) * count))
template <typename Type, typename Allocator>
inline Type* ZPushArray(Allocator* allocator, auto count = 1, std::source_location location = std::source_location::current())
{
    return static_cast<Type*>(ZPush<Type>(allocator, (sizeof(Type) * count), location));
}

// #define ZPushString(arena, count) ZPushArray(arena, char, count)
template <typename Allocator>
inline char* ZPushString(Allocator* allocator, std::integral auto count = 1, std::source_location location = std::source_location::current())
{
    return ZPushArray<char>(allocator, count, location);
};

// #define ZPushStruct(arena, type) ZPushArray(arena, type, 1)
template <typename Type, typename Allocator>
inline Type* ZPushStruct(Allocator* allocator, std::source_location location = std::source_location::current())
{
    return ZPushArray<Type>(allocator, 1, location);
}

#ifdef __cplusplus
// #define ZPushStructCtor(arena, type)          (new (ZPushStruct(arena, type)) type())
template <typename Type, typename Allocator>
inline Type* ZPushStructCtor(Allocator* arena, std::source_location location = std::source_location::current())
{
    return new (ZPushStruct<Type>(arena, location)) Type();
}

// #define ZPushStructCtorArgs(arena, type, ...) (new (ZPushStruct(arena, type)) type(__VA_ARGS__))
template <typename Type, typename Allocator, typename... ctorargs>
inline Type* ZPushStructCtorArgs(Allocator* arena, std::source_location location, ctorargs&&... args)
{
    return new (ZPushStruct<Type>(arena, location)) Type(std::forward<ctorargs>(args)...);
}

// #define ZConstruct(ptr, type)          (new (ptr) type())
template <typename Type>
inline Type* ZConstruct(void* ptr)
{
    return new (ptr) Type();
}

// #define ZConstructArgs(ptr, type, ...) (new (ptr) type(__VA_ARGS__))

template <typename Type, typename... ctorargs>
inline Type* ZConstructArgs(Type* ptr, ctorargs... args)
{
    return new (ptr) Type(args...);
}

#endif

// #define ZPushDynamicArray(pool, type)                          ((type*) (pool)->Allocate(__FILE__, __LINE__))
template <typename Type, typename Allocator>
inline Type* ZPushDynamicArray(Allocator* pool, std::source_location location = std::source_location::current())
{
    return static_cast<Type*>(pool->Allocate(location.file_name(), location.line()));
}

// #define ZAlloc(allocator, size, alignment)                     ((allocator)->Allocate((size), (alignment)))
template <typename Allocator>
inline void* ZAlloc(Allocator* allocator, std::integral auto size, std::integral auto alignment)
{
    return allocator->Allocate(size, alignment);
}

// #define ZResize(allocator, ptr, old_size, new_size, alignment) ((allocator)->Resize((ptr), (old_size), (new_size), (alignment)))

template <typename Allocator>
inline auto* ZResize(Allocator* allocator, auto ptr, std::size_t old_size, std::size_t new_size, auto alignment)
{
    return allocator->Resize(ptr, old_size, new_size, alignment);
}

// #define ZAlignof(type) ((alignof(type) < DEFAULT_ALIGNMENT) ? DEFAULT_ALIGNMENT : alignof(type))
template <typename Type>
inline constexpr std::integral auto ZAlignof()
{
    return (alignof(Type) < DEFAULT_ALIGNMENT) ? DEFAULT_ALIGNMENT : alignof(Type);
}

/*
 *
 */

inline consteval uint32_t make_magic(uint32_t first, uint32_t second, uint32_t third, uint32_t fourth)
{
    return first << 24 | second << 16 | third << 8 | fourth;
}

inline consteval uint32_t make_version(uint32_t major, uint32_t minor, uint32_t patch)
{
    return major << 16 | minor << 8 | patch;
}

inline constexpr auto ZEASSET_MAGIC      = make_magic('Z', 'A', 'S', 'T');
inline constexpr auto ZEMESH_MAGIC       = make_magic('Z', 'M', 'S', 'H');
inline constexpr auto ZEMATERIAL_MAGIC   = make_magic('Z', 'M', 'A', 'T');
inline constexpr auto ZETEXTURES_MAGIC   = make_magic('Z', 'T', 'E', 'X');
inline constexpr auto ZESCENE_MAGIC      = make_magic('Z', 'S', 'C', 'N');
inline constexpr auto ZENVMAP_MAGIC      = make_magic('Z', 'E', 'N', 'V');

inline constexpr auto ASSET_FILE_VERSION = make_version(1, 0, 0);
inline constexpr auto SCENE_FILE_VERSION = make_version(2, 1, 0);

using cstring                            = const char*;

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
