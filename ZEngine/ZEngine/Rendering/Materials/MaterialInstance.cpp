#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Materials/MaterialInstance.h>

namespace ZEngine::Rendering::Materials
{
    namespace
    {
        uint32_t GetInlineParameterLimit(uint32_t maximum_push_constant_bytes)
        {
            return maximum_push_constant_bytes < kMaxMaterialInlineParameterBytes ? maximum_push_constant_bytes : kMaxMaterialInlineParameterBytes;
        }

        bool ValidateMaterialTemplate(const MaterialTemplate* material_template, MaterialPermutationMask active_permutations)
        {
            if (!material_template || !material_template->ShaderBaseName || material_template->ShaderBaseName[0] == '\0')
                return false;
            return (active_permutations & ~material_template->SupportedPermutations) == 0;
        }
    } // namespace

    bool MaterialInstance::InitializeInline(const MaterialTemplate* material_template, MaterialPermutationMask active_permutations, const void* parameters, uint32_t parameter_byte_count, uint32_t maximum_push_constant_bytes)
    {
        if (!ValidateMaterialTemplate(material_template, active_permutations))
            return false;

        const uint32_t inline_limit = GetInlineParameterLimit(maximum_push_constant_bytes);
        if (material_template->PushConstantSize > inline_limit || parameter_byte_count != material_template->PushConstantSize || (parameter_byte_count != 0 && parameters == nullptr))
            return false;

        *this                    = {};
        Template                 = material_template;
        ActivePermutations       = active_permutations;
        ParameterByteCount       = parameter_byte_count;
        InlineParameterByteCount = parameter_byte_count;
        ParameterStorage         = MaterialParameterStorage::InlinePushConstants;
        if (parameter_byte_count != 0)
            Helpers::secure_memcpy(Parameters, sizeof(Parameters), parameters, parameter_byte_count);
        return true;
    }

    bool MaterialInstance::InitializeExternal(const MaterialTemplate* material_template, MaterialPermutationMask active_permutations, uint32_t external_parameter_index, uint32_t maximum_push_constant_bytes)
    {
        if (!ValidateMaterialTemplate(material_template, active_permutations) || external_parameter_index == kInvalidMaterialParameterIndex)
            return false;

        const uint32_t inline_limit = GetInlineParameterLimit(maximum_push_constant_bytes);
        if (inline_limit < sizeof(external_parameter_index) || material_template->PushConstantSize <= inline_limit)
            return false;

        *this                    = {};
        Template                 = material_template;
        ActivePermutations       = active_permutations;
        ParameterStorage         = MaterialParameterStorage::ExternalBuffer;
        ParameterByteCount       = material_template->PushConstantSize;
        InlineParameterByteCount = sizeof(external_parameter_index);
        ExternalParameterIndex   = external_parameter_index;
        Helpers::secure_memcpy(Parameters, sizeof(Parameters), &external_parameter_index, sizeof(external_parameter_index));
        return true;
    }

    bool MaterialInstance::IsTransparent() const
    {
        return HasMaterialPermutation(ActivePermutations, MaterialPermutation::AlphaBlend);
    }
} // namespace ZEngine::Rendering::Materials
