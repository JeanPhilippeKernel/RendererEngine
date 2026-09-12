#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Rendering::Materials
{
    /// @brief Bit flags selecting optional material shader features.
    enum class MaterialPermutation : uint64_t
    {
        None          = 0,
        AlphaTest     = 1ULL << 0,
        AlphaBlend    = 1ULL << 1,
        DoubleSided   = 1ULL << 2,
        Clearcoat     = 1ULL << 3,
        SubsurfaceSSS = 1ULL << 4,
        WithNormalMap = 1ULL << 5,
        WithEmissive  = 1ULL << 6,
    };

    /// @brief Unsigned representation used to combine material permutation flags.
    using MaterialPermutationMask = uint64_t;

    /// @brief Converts one permutation flag to its bit-mask representation.
    constexpr MaterialPermutationMask ToMaterialPermutationMask(MaterialPermutation permutation)
    {
        return static_cast<MaterialPermutationMask>(permutation);
    }

    /// @brief Tests whether a permutation mask contains every flag in `permutation`.
    constexpr bool HasMaterialPermutation(MaterialPermutationMask mask, MaterialPermutation permutation)
    {
        const MaterialPermutationMask bits = ToMaterialPermutationMask(permutation);
        return (mask & bits) == bits;
    }

    /// @brief Maximum number of bytes stored directly in a material instance.
    inline constexpr uint32_t kMaxMaterialInlineParameterBytes = 128;

    /// @brief Describes a shader family and the variants it permits.
    struct MaterialTemplate
    {
        cstring                 ShaderBaseName        = nullptr;
        MaterialPermutationMask SupportedPermutations = 0;
        uint32_t                PushConstantSize      = 0;
    };

    /// @brief Identifies the graphics context for material shader selection.
    enum class PassContext : uint8_t
    {
        Lit = 0,
        DepthPrePass,
        ShadowDepth,
        Wireframe,
        COUNT,
    };

    /// @brief Stable shader-family and feature identity resolved for one pass context.
    struct ShaderVariantKey
    {
        uint64_t                BaseShaderHash  = 0;
        MaterialPermutationMask PermutationMask = 0;
        PassContext             Context         = PassContext::Lit;
        uint8_t                 Reserved[7]     = {};

        constexpr bool          operator==(const ShaderVariantKey& other) const
        {
            return BaseShaderHash == other.BaseShaderHash && PermutationMask == other.PermutationMask && Context == other.Context;
        }
    };

    /// @brief Returns the stable FNV-1a identity of a null-terminated shader base name.
    uint64_t                HashMaterialShaderBaseName(cstring shader_base_name);

    /// @brief Removes flags a template does not support and flags irrelevant to `context`.
    MaterialPermutationMask ResolveMaterialPermutationMask(const MaterialTemplate& material_template, MaterialPermutationMask active_permutations, PassContext context);
} // namespace ZEngine::Rendering::Materials
