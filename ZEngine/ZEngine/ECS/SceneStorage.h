#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    enum class SceneStorageAdmission : uint8_t
    {
        Acquired,
        Busy,
        Unconfigured,
        InvalidArgument
    };
    struct SceneStorageSlot
    {
        Core::Memory::ArenaAllocator* Arena = nullptr;
        PaddedAtomic<bool>*           InUse = nullptr;
    };

    /// Exclusive, move-only lease over pre-reserved scene storage. The slot and
    /// its backing allocator must outlive every lease. Destroy all arena users
    /// before releasing it; acquisition never creates another OS reservation.
    class SceneStorageLease final
    {
    public:
        static constexpr size_t MinimumBytes = ZMega(8);

        SceneStorageLease()                  = default;
        ~SceneStorageLease();
        SceneStorageLease(const SceneStorageLease&)            = delete;
        SceneStorageLease& operator=(const SceneStorageLease&) = delete;
        SceneStorageLease(SceneStorageLease&& other) noexcept;
        SceneStorageLease&                          operator=(SceneStorageLease&& other) noexcept;

        /// Requires an empty destination; failure leaves it and occupied slots intact.
        [[nodiscard]] static bool                   Acquire(Core::Containers::ArrayView<const SceneStorageSlot> slots, SceneStorageLease* out, size_t minimum_bytes = MinimumBytes);
        /// Busy is retryable; absent/undersized configured storage is not.
        [[nodiscard]] static SceneStorageAdmission  Admit(Core::Containers::ArrayView<const SceneStorageSlot> slots, SceneStorageLease* out, size_t minimum_bytes = MinimumBytes);
        void                                        Reset() noexcept;
        [[nodiscard]] Core::Memory::ArenaAllocator* Arena() const noexcept
        {
            return m_arena;
        }

    private:
        Core::Memory::ArenaAllocator* m_arena  = nullptr;
        PaddedAtomic<bool>*           m_in_use = nullptr;
    };
} // namespace ZEngine::ECS
