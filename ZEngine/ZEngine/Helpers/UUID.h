#pragma once
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>
#include <string_view>

namespace ZEngine::Helpers
{
    // Owned UUID output from exactly the canonical lowercase display spelling.
    // No allocation, normalization, or acceptance of braces/whitespace/suffixes.
    [[nodiscard]] inline bool ParseCanonicalUUID(cstring text, size_t size, uuids::uuid* output, bool allow_nil = false)
    {
        if (!text || size != 36 || !output)
            return false;
        for (size_t i = 0; i < size; ++i)
        {
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (text[i] != '-')
                    return false;
            }
            else if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f')))
                return false;
        }
        const auto parsed = uuids::uuid::from_string(std::string_view(text, size));
        if (!parsed || (!allow_nil && parsed->is_nil()))
            return false;
        *output = *parsed;
        return true;
    }
} // namespace ZEngine::Helpers
