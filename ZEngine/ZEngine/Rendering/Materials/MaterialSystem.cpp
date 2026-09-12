#include <ZEngine/Rendering/Materials/MaterialSystem.h>

namespace ZEngine::Rendering::Materials
{
    namespace
    {
        uint32_t ClampPrewarmRequestCount(uint32_t requested_count)
        {
            return requested_count < kMaxMaterialPrewarmRequests ? requested_count : kMaxMaterialPrewarmRequests;
        }

        bool IsPassContextValid(PassContext context)
        {
            return static_cast<uint32_t>(context) < static_cast<uint32_t>(PassContext::COUNT);
        }

        bool IsDuplicateRequest(const MaterialPrewarmRequest* requests, uint32_t request_count, const MaterialPrewarmRequest& candidate)
        {
            for (uint32_t request_index = 0; request_index < request_count; ++request_index)
            {
                if (requests[request_index].Template == candidate.Template && requests[request_index].Variant == candidate.Variant)
                    return true;
            }
            return false;
        }
    } // namespace

    void MaterialSystem::Initialize(uint32_t maximum_push_constant_bytes)
    {
        m_maximum_inline_parameter_bytes = maximum_push_constant_bytes < kMaxMaterialInlineParameterBytes ? maximum_push_constant_bytes : kMaxMaterialInlineParameterBytes;
        for (uint32_t context_index = 0; context_index < static_cast<uint32_t>(PassContext::COUNT); ++context_index)
            m_recipes[context_index] = {};
    }

    uint32_t MaterialSystem::GetMaximumInlineParameterBytes() const
    {
        return m_maximum_inline_parameter_bytes;
    }

    bool MaterialSystem::CreateInstance(const MaterialTemplate* material_template, MaterialPermutationMask active_permutations, const void* inline_parameters, uint32_t inline_parameter_byte_count, uint32_t external_parameter_index, MaterialInstance* out_instance) const
    {
        if (!out_instance || !material_template)
            return false;

        if (material_template->PushConstantSize <= m_maximum_inline_parameter_bytes)
            return out_instance->InitializeInline(material_template, active_permutations, inline_parameters, inline_parameter_byte_count, m_maximum_inline_parameter_bytes);

        return out_instance->InitializeExternal(material_template, active_permutations, external_parameter_index, m_maximum_inline_parameter_bytes);
    }

    ShaderVariantKey MaterialSystem::ResolveVariantKey(const MaterialInstance& material, PassContext context) const
    {
        ZENGINE_VALIDATE_ASSERT(material.Template != nullptr, "Material instances require a material template")
        ZENGINE_VALIDATE_ASSERT(IsPassContextValid(context), "PassContext::COUNT is not a material pass context")

        ShaderVariantKey key = {};
        key.BaseShaderHash   = HashMaterialShaderBaseName(material.Template->ShaderBaseName);
        key.PermutationMask  = ResolveMaterialPermutationMask(*material.Template, material.ActivePermutations, context);
        key.Context          = context;
        return key;
    }

    bool MaterialSystem::RegisterGraphicsPrewarmRecipe(PassContext context, const MaterialGraphicsPrewarmRecipe& recipe)
    {
        if (!IsPassContextValid(context) || !recipe.BuildCreateInfo)
            return false;

        m_recipes[static_cast<uint32_t>(context)] = recipe;
        return true;
    }

    uint32_t MaterialSystem::CollectPrewarmRequests(Core::Containers::ArrayView<const MaterialInstance> materials, Core::Containers::ArrayView<const PassContext> contexts, const PrewarmBudget& budget, MaterialPrewarmRequest* out_requests, uint32_t out_request_capacity, MaterialPrewarmResult* out_result) const
    {
        MaterialPrewarmResult result        = {};
        const uint32_t        max_count     = ClampPrewarmRequestCount(budget.MaxPermutations);
        const uint32_t        capacity      = out_request_capacity < max_count ? out_request_capacity : max_count;
        uint32_t              request_count = 0;

        ZENGINE_VALIDATE_ASSERT(capacity == 0 || out_requests != nullptr, "Material prewarm output is null")
        for (uint32_t material_index = 0; material_index < materials.size(); ++material_index)
        {
            const MaterialInstance& material = materials[material_index];
            if (!material.Template)
                continue;

            for (uint32_t context_index = 0; context_index < contexts.size(); ++context_index)
            {
                const PassContext context = contexts[context_index];
                if (!IsPassContextValid(context))
                    continue;

                ++result.CandidateCount;
                const MaterialPrewarmRequest candidate = {
                    .Template = material.Template,
                    .Variant  = ResolveVariantKey(material, context),
                };
                if (IsDuplicateRequest(out_requests, request_count, candidate))
                    continue;

                if (request_count == capacity)
                {
                    ++result.BudgetSkipped;
                    continue;
                }
                out_requests[request_count] = candidate;
                ++request_count;
            }
        }

        if (out_result)
            *out_result = result;
        return request_count;
    }

    MaterialPrewarmResult MaterialSystem::PrewarmForMaterials(Core::Containers::ArrayView<const MaterialInstance> materials, Core::Containers::ArrayView<const PassContext> contexts, Renderers::Pipelines::PSOCache& cache, const PrewarmBudget& budget, void* callback_context, Renderers::Pipelines::PSOPipelineReadyFn callback) const
    {
        MaterialPrewarmRequest requests[kMaxMaterialPrewarmRequests] = {};
        MaterialPrewarmResult  result                                = {};
        const uint32_t         request_count                         = CollectPrewarmRequests(materials, contexts, budget, requests, kMaxMaterialPrewarmRequests, &result);

        for (uint32_t request_index = 0; request_index < request_count; ++request_index)
        {
            const MaterialPrewarmRequest&        request = requests[request_index];
            const MaterialGraphicsPrewarmRecipe& recipe  = m_recipes[static_cast<uint32_t>(request.Variant.Context)];
            if (!recipe.BuildCreateInfo)
            {
                ++result.MissingRecipes;
                continue;
            }

            VkGraphicsPipelineCreateInfo create_info       = {};
            uint32_t                     shader_generation = 0;
            if (!recipe.BuildCreateInfo(recipe.Context, *request.Template, request.Variant, &create_info, &shader_generation))
            {
                ++result.InvalidRecipes;
                continue;
            }

            (void) cache.RequestGraphicsPipelineAsync(create_info, shader_generation, callback_context, callback, budget.Critical);
            ++result.RequestedCount;
        }
        return result;
    }
} // namespace ZEngine::Rendering::Materials
