#pragma once
#include <ZEngine/Rendering/Materials/MaterialTemplate.h>
#include <cstdint>

namespace ZEngine::Rendering::Materials
{
    /// @brief Selects whether a material's parameter block is bound inline or externally.
    enum class MaterialParameterStorage : uint8_t
    {
        InlinePushConstants = 0,
        ExternalBuffer,
    };

    /// @brief Sentinel used when no external material-parameter allocation exists.
    inline constexpr uint32_t kInvalidMaterialParameterIndex = UINT32_MAX;

    /// @brief A concrete material template, variant flags, and per-material parameter binding.
    struct MaterialInstance
    {
        const MaterialTemplate*  Template                                     = nullptr;
        MaterialPermutationMask  ActivePermutations                           = 0;
        MaterialParameterStorage ParameterStorage                             = MaterialParameterStorage::InlinePushConstants;
        uint32_t                 ParameterByteCount                           = 0;
        uint32_t                 InlineParameterByteCount                     = 0;
        uint32_t                 ExternalParameterIndex                       = kInvalidMaterialParameterIndex;
        uint8_t                  Parameters[kMaxMaterialInlineParameterBytes] = {};

        /// @brief Initializes an inline push-constant payload that fits the device limit.
        bool                     InitializeInline(const MaterialTemplate* material_template, MaterialPermutationMask active_permutations, const void* parameters, uint32_t parameter_byte_count, uint32_t maximum_push_constant_bytes);

        /// @brief Initializes a material whose complete parameter block resides in an external UBO or SSBO.
        bool                     InitializeExternal(const MaterialTemplate* material_template, MaterialPermutationMask active_permutations, uint32_t external_parameter_index, uint32_t maximum_push_constant_bytes);

        /// @brief Returns true when conventional alpha blending requires back-to-front ordering.
        bool                     IsTransparent() const;
    };
} // namespace ZEngine::Rendering::Materials
