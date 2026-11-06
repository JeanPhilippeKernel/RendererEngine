#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstddef>
#include <cstdint>

namespace ZEngine::Helpers
{
    // Allocation-free, length-delimited validation. Embedded NUL is never text.
    // Path callers additionally reject C0/C1 controls, without normalizing Unicode.
    [[nodiscard]] inline bool IsValidUTF8(cstring text, size_t size, bool allow_controls = true)
    {
        if (!text && size)
        {
            return false;
        }
        for (size_t i = 0; i < size;)
        {
            const auto first = static_cast<uint8_t>(text[i++]);
            if (!first || (!allow_controls && (first < 0x20 || first == 0x7f)))
            {
                return false;
            }
            if (first < 0x80)
            {
                continue;
            }
            uint32_t value   = 0;
            uint32_t minimum = 0;
            size_t   count   = 0;
            if (first >= 0xc2 && first <= 0xdf)
            {
                value   = first & 0x1fu;
                minimum = 0x80;
                count   = 1;
            }
            else if (first >= 0xe0 && first <= 0xef)
            {
                value   = first & 0x0fu;
                minimum = 0x800;
                count   = 2;
            }
            else if (first >= 0xf0 && first <= 0xf4)
            {
                value   = first & 0x07u;
                minimum = 0x10000;
                count   = 3;
            }
            else
            {
                return false;
            }
            if (count > size - i)
            {
                return false;
            }
            for (size_t j = 0; j < count; ++j)
            {
                const auto next = static_cast<uint8_t>(text[i++]);
                if ((next & 0xc0u) != 0x80u)
                {
                    return false;
                }
                value = (value << 6) | (next & 0x3fu);
            }
            if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) || (!allow_controls && value <= 0x9f))
            {
                return false;
            }
        }
        return true;
    }
} // namespace ZEngine::Helpers
