#pragma once
#include <string_view>

namespace ZEngine::ECS
{
    [[nodiscard]] inline bool IsValidSceneSchemaKey(std::string_view key)
    {
        if (key.empty() || key.size() >= 128)
            return false;
        for (const unsigned char value : key)
            if (value < 0x21u || value > 0x7eu || value == '/' || value == '\\' || value == '"')
                return false;
        return true;
    }
} // namespace ZEngine::ECS
