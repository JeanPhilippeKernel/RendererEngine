#pragma once
#include <ZEngine/Managers/CookedCpuAssetSet.h>

namespace ZEngine::Managers
{
    /// Worker-confined implementation detail of the bounded closure cache.
    class CookedCpuAssetPool final
    {
    public:
        CookedCpuAssetPool() = default;
        ~CookedCpuAssetPool();
        CookedCpuAssetPool(const CookedCpuAssetPool&)            = delete;
        CookedCpuAssetPool& operator=(const CookedCpuAssetPool&) = delete;

    private:
        friend class CookedCpuAssetSet;
        friend class CookedCpuClosureCache;
        struct Entry;
        static constexpr uint32_t Invalid = UINT32_MAX;
        [[nodiscard]] bool        Initialize(Core::Memory::IAllocationRoute&, uint32_t capacity);
        [[nodiscard]] bool        Shutdown() noexcept;
        [[nodiscard]] uint32_t    Find(const CookedCpuAssetVersion&) const noexcept;
        [[nodiscard]] bool        Retain(uint32_t) noexcept;
        [[nodiscard]] bool        Publish(const CookedCpuAssetVersion&, void*, uint32_t*) noexcept;
        void                      Release(uint32_t) noexcept;
        [[nodiscard]] bool        HasCapacity() const noexcept
        {
            return m_free != Invalid;
        }
        [[nodiscard]] uint32_t          Bucket(const CookedCpuAssetVersion&) const noexcept;
        Core::Memory::IAllocationRoute* m_route        = nullptr;
        Entry*                          m_entries      = nullptr;
        uint32_t*                       m_buckets      = nullptr;
        uint32_t                        m_capacity     = 0;
        uint32_t                        m_bucket_count = 0;
        uint32_t                        m_free         = Invalid;
        uint32_t                        m_live         = 0;
    };
} // namespace ZEngine::Managers
