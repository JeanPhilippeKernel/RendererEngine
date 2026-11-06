#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstddef>
#include <new>
#include <type_traits>

namespace ZEngine::Core::Memory
{
    // Native writable mappings, with no CRT allocator/control-block metadata.
    // Zero means unsupported/invalid/overflow. Queries never map or commit pages.
    [[nodiscard]] size_t NativePageSize() noexcept;
    [[nodiscard]] size_t NativePageFootprint(size_t bytes, size_t page_size) noexcept;
    // Input is an already page-rounded positive footprint. A successful mapping
    // contains exactly that many writable bytes and is naturally page-aligned.
    [[nodiscard]] void*  AllocateNativePages(size_t page_bytes) noexcept;
    [[nodiscard]] bool   FreeNativePages(void* mapping, size_t page_bytes) noexcept;

    // For fixed-size, normally aligned FINAL independently retained owners.
    // One mapping per object, no hidden header/map or additional allocation.
    // Dynamic deletion through a virtual base finds this matching deallocator.
    // Not a small-allocation route: containers/files use their existing pools.
    template <typename T>
    class NativePageObject
    {
    public:
        [[nodiscard]] static size_t NativeStorageBytes() noexcept
        {
            static_assert(std::is_final_v<T>, "Native page objects need an exact final dynamic type.");
            const auto page = NativePageSize();
            return page && alignof(T) <= page ? NativePageFootprint(sizeof(T), page) : 0;
        }
        [[nodiscard]] static void* operator new(size_t bytes, const std::nothrow_t&) noexcept
        {
            const auto footprint = NativeStorageBytes();
            return bytes == sizeof(T) && footprint ? AllocateNativePages(footprint) : nullptr;
        }
        static void operator delete(void* pointer) noexcept
        {
            if (pointer)
            {
                [[maybe_unused]] const bool freed = FreeNativePages(pointer, NativeStorageBytes());
                ZENGINE_VALIDATE_ASSERT(freed, "Could not release an owned native object mapping.");
            }
        }
        static void operator delete(void* pointer, const std::nothrow_t&) noexcept
        {
            operator delete(pointer);
        }

    protected:
        ~NativePageObject() = default;
    };
} // namespace ZEngine::Core::Memory
