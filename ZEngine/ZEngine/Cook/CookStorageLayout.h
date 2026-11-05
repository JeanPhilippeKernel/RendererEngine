#pragma once
#include <cstddef>
#include <cstdint>

namespace ZEngine::Cook::Detail
{
    // Checked packed-table layout, shared by exact-size cook owners. A failed
    // addition preserves both the layout and caller's previous offset.
    class CookStorageLayout
    {
    public:
        template <typename T>
        [[nodiscard]] bool Add(size_t count, size_t* offset) noexcept
        {
            if (!offset || m_bytes > SIZE_MAX - (alignof(T) - 1))
                return false;
            const size_t aligned = (m_bytes + alignof(T) - 1) & ~(alignof(T) - 1);
            if (count > (SIZE_MAX - aligned) / sizeof(T))
                return false;
            *offset = aligned;
            m_bytes = aligned + count * sizeof(T);
            return true;
        }

        [[nodiscard]] size_t Size() const noexcept
        {
            return m_bytes;
        }

    private:
        size_t m_bytes = 0;
    };
} // namespace ZEngine::Cook::Detail
