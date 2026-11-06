#pragma once
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Core/Memory/AllocationRoute.h>

namespace ZEngine::Cook
{
    // Install with the shared image/YAML allocation scope for the entire parse/decode.
    // Temporary old/new realloc blocks both use the call's checked allocator.
    // The first refusal is sticky even if stb subsequently finds a successful
    // probe/format fallback: check Error() as well as the decoder return value.
    // The shared working-budget hook coalesces nested fixed-state reservations
    // in the same ledger without allocating. Tokens may drain non-LIFO and must
    // all release before this route, including after a sticky refusal.
    class CookCodecAllocationRoute final : public Core::Memory::IAllocationRoute, private Core::Memory::IWorkingMemoryBudget
    {
    public:
        explicit CookCodecAllocationRoute(ICookAllocator& allocator) noexcept : m_allocator(&allocator) {}
        ~CookCodecAllocationRoute() override;
        CookCodecAllocationRoute(const CookCodecAllocationRoute&)                                    = delete;
        CookCodecAllocationRoute&                         operator=(const CookCodecAllocationRoute&) = delete;

        [[nodiscard]] void*                               Allocate(size_t bytes) noexcept override;
        [[nodiscard]] void*                               Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept override;
        void                                              Free(void* pointer) noexcept override;
        [[nodiscard]] Core::Memory::IWorkingMemoryBudget* WorkingMemoryBudget() noexcept override
        {
            return this;
        }
        [[nodiscard]] Core::VFS::VFSError Error() const noexcept
        {
            return m_error;
        }

    private:
        [[nodiscard]] bool  TryReserveWorkingBytes(size_t bytes) noexcept override;
        void                ReleaseWorkingBytes(size_t bytes) noexcept override;

        ICookAllocator*     m_allocator = nullptr;
        Core::VFS::VFSError m_error     = Core::VFS::VFSError::OK;
        CookMemoryCharge    m_working   = {};
    };
} // namespace ZEngine::Cook
