#pragma once
#include <cmath>
#include <cstdint>
#include <string_view>

namespace ZEngine::Rendering::Materials
{
    // Values are shared with material_alpha.glsl. Legacy is intentionally not
    // inferred as BLEND: old cooked assets discarded the original glTF mode.
    enum class AlphaMode : uint32_t
    {
        Legacy = 0,
        Opaque = 1,
        Mask   = 2,
        Blend  = 3,
    };

    constexpr const char* AlphaModeName(AlphaMode mode)
    {
        switch (mode)
        {
            case AlphaMode::Opaque:
                return "OPAQUE";
            case AlphaMode::Mask:
                return "MASK";
            case AlphaMode::Blend:
                return "BLEND";
            default:
                return "LEGACY";
        }
    }

    constexpr AlphaMode ParseAlphaMode(std::string_view mode)
    {
        if (mode == "OPAQUE")
            return AlphaMode::Opaque;
        if (mode == "MASK")
            return AlphaMode::Mask;
        if (mode == "BLEND")
            return AlphaMode::Blend;
        return AlphaMode::Legacy;
    }

    inline bool IsConstantFullyTransparent(AlphaMode mode, float alpha, float cutoff, bool has_albedo_map)
    {
        if (!std::isfinite(alpha) || alpha < 0.f)
            return false;
        // Explicit modes multiply sampled alpha by this factor, so zero remains
        // provably invisible even without reading the texture. Legacy replaces
        // the factor instead and cannot make that inference for textured assets.
        if (mode == AlphaMode::Blend)
            return alpha == 0.f;
        if (mode == AlphaMode::Mask && alpha == 0.f && std::isfinite(cutoff) && cutoff > 0.f)
            return true;
        if (has_albedo_map)
            return false;
        return (mode == AlphaMode::Mask || mode == AlphaMode::Legacy) && std::isfinite(cutoff) && cutoff > 0.f && alpha < cutoff;
    }

    inline bool TransparentDepthBefore(float left_depth, uint32_t left_order, float right_depth, uint32_t right_order)
    {
        if (std::isnan(left_depth) != std::isnan(right_depth))
            return !std::isnan(left_depth);
        if (left_depth != right_depth && !std::isnan(left_depth))
            return left_depth > right_depth;
        return left_order < right_order;
    }

    struct AlphaInfo
    {
        AlphaMode Mode        = AlphaMode::Legacy;
        float     BaseOpacity = 1.f;
        float     Cutoff      = 0.f;
    };
} // namespace ZEngine::Rendering::Materials
