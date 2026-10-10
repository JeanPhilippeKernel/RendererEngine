#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <atomic>
#include <cstddef>

// Forward-declare the TLSF opaque handle so callers do not need to include tlsf.h.
typedef void* tlsf_t;

namespace ZEngine::Core::Memory
{
    /// @brief Arena-backed slab that uses the TLSF allocator for O(1) variable-size
    ///        allocations with individual free.
    ///
    /// Fills the gap between ArenaAllocator (no individual free) and PoolAllocator
    /// (fixed chunk size): upload decode buffers, asset metadata containers, and any
    /// allocation whose size varies at runtime and whose lifetime is individual.
    ///
    /// The backing memory is carved from a parent ArenaAllocator exactly once at Init.
    /// All subsequent Alloc/Free calls never touch the arena.
    ///
    /// Thread safety: Alloc, Realloc, and Free are all protected by an internal
    /// atomic spinlock. The typical case is contention-free (one worker calls Alloc,
    /// the render thread calls Free only on upload completion), so the lock overhead
    /// is ~5 ns uncontended — acceptable for Phase 1. A deferred-free queue (#690)
    /// can replace the spinlock in a future pass to keep Alloc fully lock-free.
    struct TLSFSlab
    {
        /// @brief Carve `bytes` from `arena` and initialise the TLSF pool over it.
        /// @param arena Parent arena that provides the backing memory.
        /// @param bytes Total slab size in bytes. Must be > tlsf_size() (~3 KB overhead).
        void   Init(ArenaAllocator* arena, size_t bytes);

        /// @brief Allocate `n` bytes from the slab. Asserts on exhaustion.
        /// @param n Requested byte count. Must be > 0.
        /// @returns Aligned pointer into the slab. Never null — asserts on failure.
        void*  Alloc(size_t n);

        /// @brief Reallocate `ptr` to `n` bytes. O(1) if in-place, O(n) copy otherwise.
        /// @param ptr Previously Alloc'd pointer, or nullptr (delegates to Alloc).
        /// @param n   New size in bytes.
        void*  Realloc(void* ptr, size_t n);

        /// @brief Return `ptr` to the slab. No-op on nullptr. O(1), coalesces neighbours.
        /// @param ptr Pointer previously returned by Alloc or Realloc, or nullptr.
        void   Free(void* ptr);

        /// @brief Destroy the TLSF pool. Does NOT free the backing memory — the parent
        ///        arena owns it and reclaims it on Shutdown.
        void   Shutdown();

        /// @brief Bytes consumed by TLSF metadata (~3 KB per slab). Diagnostic use only.
        /// @returns Overhead in bytes.
        size_t Overhead() const;

        void*  Backing = nullptr; ///< Raw backing buffer carved from the parent arena.
        tlsf_t Pool    = nullptr; ///< TLSF pool handle (opaque pointer).

    private:
        // Atomic spinlock — protects concurrent Alloc/Free across threads.
        // std::atomic_flag is guaranteed lock-free on all platforms.
        mutable std::atomic_flag m_lock = ATOMIC_FLAG_INIT;

        void                     AcquireLock() const
        {
            while (m_lock.test_and_set(std::memory_order_acquire))
            {
            }
        }
        void ReleaseLock() const
        {
            m_lock.clear(std::memory_order_release);
        }
    };

} // namespace ZEngine::Core::Memory
