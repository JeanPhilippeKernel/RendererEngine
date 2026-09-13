#include <ZEngine/Rendering/Materials/MaterialTemplate.h>

namespace ZEngine::Rendering::Materials
{
    uint64_t HashMaterialShaderBaseName(cstring shader_base_name)
    {
        ZENGINE_VALIDATE_ASSERT(shader_base_name != nullptr && shader_base_name[0] != '\0', "Material shader base name is required")

        uint64_t hash = UINT64_C(14695981039346656037);
        for (const unsigned char* character = reinterpret_cast<const unsigned char*>(shader_base_name); *character != '\0'; ++character)
        {
            hash ^= *character;
            hash *= UINT64_C(1099511628211);
        }
        return hash;
    }

    MaterialPermutationMask ResolveMaterialPermutationMask(const MaterialTemplate& material_template, MaterialPermutationMask active_permutations, PassContext context)
    {
        MaterialPermutationMask result = active_permutations & material_template.SupportedPermutations;
        switch (context)
        {
            case PassContext::DepthPrePass:
            case PassContext::ShadowDepth:
            {
                // Alpha-tested geometry still needs a fragment discard in depth-only
                // passes, while double-sidedness remains rasterization state.
                constexpr MaterialPermutationMask depth_safe  = ToMaterialPermutationMask(MaterialPermutation::AlphaTest) | ToMaterialPermutationMask(MaterialPermutation::DoubleSided);
                result                                       &= depth_safe;
                break;
            }
            case PassContext::Lit:
            case PassContext::Wireframe:
                break;
            case PassContext::COUNT:
                ZENGINE_VALIDATE_ASSERT(false, "PassContext::COUNT is not a material pass context")
                return 0;
        }
        return result;
    }
} // namespace ZEngine::Rendering::Materials
