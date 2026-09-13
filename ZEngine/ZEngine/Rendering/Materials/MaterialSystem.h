#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Rendering/Materials/MaterialInstance.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>

namespace ZEngine::Rendering::Materials
{
    /// @brief Maximum number of material variants requested in one bounded prewarm pass.
    inline constexpr uint32_t kMaxMaterialPrewarmRequests = Renderers::Pipelines::kMaxPSOAsyncJobs;

    /// @brief Bounds asynchronous PSO requests issued while prewarming a scene.
    struct PrewarmBudget
    {
        uint32_t MaxPermutations = kMaxMaterialPrewarmRequests;
        bool     Critical        = false;
    };

    /// @brief A unique template-and-variant pair selected for PSO prewarming.
    struct MaterialPrewarmRequest
    {
        const MaterialTemplate* Template = nullptr;
        ShaderVariantKey        Variant  = {};
    };

    /// @brief Counts decisions made by one bounded material prewarm pass.
    struct MaterialPrewarmResult
    {
        uint32_t CandidateCount = 0;
        uint32_t RequestedCount = 0;
        uint32_t MissingRecipes = 0;
        uint32_t InvalidRecipes = 0;
        uint32_t BudgetSkipped  = 0;
    };

    /// @brief Builds a fully-valid graphics pipeline recipe for a resolved material variant.
    /// @details The callback owns every pointer referenced by `out_create_info` until it
    /// returns. PSOCache canonicalizes the request before returning and never retains
    /// those pointers.
    using MaterialGraphicsPrewarmRecipeFn = bool (*)(void* context, const MaterialTemplate& material_template, const ShaderVariantKey& variant, VkGraphicsPipelineCreateInfo* out_create_info, uint32_t* out_shader_generation);

    /// @brief Caller-owned recipe provider registered for a material pass context.
    struct MaterialGraphicsPrewarmRecipe
    {
        void*                           Context         = nullptr;
        MaterialGraphicsPrewarmRecipeFn BuildCreateInfo = nullptr;
    };

    /// @brief Resolves material shader variants and sends valid graphics recipes to PSOCache.
    class MaterialSystem
    {
    public:
        /// @brief Sets the selected device's maximum push-constant size.
        void                  Initialize(uint32_t maximum_push_constant_bytes);

        /// @brief Returns the device-limited inline parameter capacity used by new instances.
        uint32_t              GetMaximumInlineParameterBytes() const;

        /// @brief Initializes `out_instance` with inline data or an external spill binding as required.
        bool                  CreateInstance(const MaterialTemplate* material_template, MaterialPermutationMask active_permutations, const void* inline_parameters, uint32_t inline_parameter_byte_count, uint32_t external_parameter_index, MaterialInstance* out_instance) const;

        /// @brief Resolves the canonical shader variant for one material and pass context.
        ShaderVariantKey      ResolveVariantKey(const MaterialInstance& material, PassContext context) const;

        /// @brief Registers the render-pass-specific provider of valid graphics PSO creation inputs.
        bool                  RegisterGraphicsPrewarmRecipe(PassContext context, const MaterialGraphicsPrewarmRecipe& recipe);

        /// @brief Enumerates the bounded, deterministic, duplicate-free variants without submitting GPU work.
        uint32_t              CollectPrewarmRequests(Core::Containers::ArrayView<const MaterialInstance> materials, Core::Containers::ArrayView<const PassContext> contexts, const PrewarmBudget& budget, MaterialPrewarmRequest* out_requests, uint32_t out_request_capacity, MaterialPrewarmResult* out_result = nullptr) const;

        /// @brief Requests each resolved graphics PSO asynchronously through the registered recipe provider.
        MaterialPrewarmResult PrewarmForMaterials(Core::Containers::ArrayView<const MaterialInstance> materials, Core::Containers::ArrayView<const PassContext> contexts, Renderers::Pipelines::PSOCache& cache, const PrewarmBudget& budget, void* callback_context = nullptr, Renderers::Pipelines::PSOPipelineReadyFn callback = nullptr) const;

    private:
        uint32_t                      m_maximum_inline_parameter_bytes                     = kMaxMaterialInlineParameterBytes;
        MaterialGraphicsPrewarmRecipe m_recipes[static_cast<uint32_t>(PassContext::COUNT)] = {};
    };
} // namespace ZEngine::Rendering::Materials
