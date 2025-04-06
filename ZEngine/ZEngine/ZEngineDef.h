#pragma once
#include <Logging/LoggerDefinition.h>

#define BIT(x)                 (1 << (x))
#define ZENGINE_EXIT_FAILURE() exit(EXIT_FAILURE);

#define ZENGINE_KEYCODE        ZEngine::Windows::Inputs::GlfwKeyCode

#ifdef _MSC_VER
#define ZENGINE_DEBUG_BREAK() __debugbreak();
#elif defined(__APPLE__)
#include <signal.h>
#define ZENGINE_DEBUG_BREAK() __builtin_trap();
#else
#error "Platform not supported!"
#endif

#define ZENGINE_VALIDATE_ASSERT(condition, message) \
    {                                               \
        if (!(condition))                           \
        {                                           \
            ZENGINE_CORE_CRITICAL(message)          \
            assert(condition&& message);            \
            ZENGINE_DEBUG_BREAK()                   \
        }                                           \
    }

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

#define ZRawPtr(X)          X*

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
#define ZPushStructCtor(arena, type) (new (ZPushStruct(arena, type)) type())
#endif

#define ZPushDynamicArray(pool, type)                          ((type*) (pool)->Allocate(__FILE__, __LINE__))
#define ZAlloc(allocator, size, alignment)                     ((allocator)->Allocate((size), (alignment)))
#define ZResize(allocator, ptr, old_size, new_size, alignment) ((allocator)->Resize((ptr), (old_size), (new_size), (alignment)))
#define ZAlignof(type)                                         ((alignof(type) < DEFAULT_ALIGNMENT) ? DEFAULT_ALIGNMENT : alignof(type))
