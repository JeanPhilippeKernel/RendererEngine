#include <ZEngine/ECS/SceneStorage.h>
#include <utility>

namespace ZEngine::ECS
{
    SceneStorageLease::~SceneStorageLease()
    {
        Reset();
    }

    SceneStorageLease::SceneStorageLease(SceneStorageLease&& other) noexcept : m_arena(std::exchange(other.m_arena, nullptr)), m_in_use(std::exchange(other.m_in_use, nullptr)) {}

    SceneStorageLease& SceneStorageLease::operator=(SceneStorageLease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_arena  = std::exchange(other.m_arena, nullptr);
            m_in_use = std::exchange(other.m_in_use, nullptr);
        }
        return *this;
    }

    bool SceneStorageLease::Acquire(Core::Containers::ArrayView<const SceneStorageSlot> slots, SceneStorageLease* out, size_t minimum_bytes)
    {
        return Admit(slots, out, minimum_bytes) == SceneStorageAdmission::Acquired;
    }

    SceneStorageAdmission SceneStorageLease::Admit(Core::Containers::ArrayView<const SceneStorageSlot> slots, SceneStorageLease* out, size_t minimum_bytes)
    {
        if (!out || out->m_arena || !minimum_bytes || (slots.size() && !slots.data()))
        {
            return SceneStorageAdmission::InvalidArgument;
        }
        bool configured = false;
        for (const auto& slot : slots)
        {
            if (!slot.Arena || !slot.InUse || !slot.Arena->m_memory || slot.Arena->m_total_size < minimum_bytes)
            {
                continue;
            }
            configured    = true;
            bool expected = false;
            if (!slot.InUse->value.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                continue;
            }
            slot.Arena->Clear();
            out->m_arena  = slot.Arena;
            out->m_in_use = slot.InUse;
            return SceneStorageAdmission::Acquired;
        }
        return configured ? SceneStorageAdmission::Busy : SceneStorageAdmission::Unconfigured;
    }

    void SceneStorageLease::Reset() noexcept
    {
        if (m_arena)
        {
            m_arena->Clear();
            m_in_use->value.store(false, std::memory_order_release);
            m_arena  = nullptr;
            m_in_use = nullptr;
        }
    }
} // namespace ZEngine::ECS
