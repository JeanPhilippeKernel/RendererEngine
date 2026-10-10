#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Materials/DrawSorter.h>
#include <ZEngine/Rendering/Materials/MaterialSystem.h>
#include <gtest/gtest.h>
#include <limits>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Rendering::Materials;

namespace
{
    constexpr MaterialPermutationMask kAllTestPermutations = ToMaterialPermutationMask(MaterialPermutation::AlphaTest) | ToMaterialPermutationMask(MaterialPermutation::AlphaBlend) | ToMaterialPermutationMask(MaterialPermutation::DoubleSided) | ToMaterialPermutationMask(MaterialPermutation::Clearcoat) | ToMaterialPermutationMask(MaterialPermutation::SubsurfaceSSS) | ToMaterialPermutationMask(MaterialPermutation::WithNormalMap) | ToMaterialPermutationMask(MaterialPermutation::WithEmissive);

    bool                              RejectPrewarmRecipe(void*, const MaterialTemplate&, const ShaderVariantKey&, VkGraphicsPipelineCreateInfo*, uint32_t*)
    {
        return false;
    }
} // namespace

TEST(MaterialSystemTest, ResolvesOnlySupportedPassAppropriatePermutations)
{
    MaterialSystem system = {};
    system.Initialize(128);

    MaterialTemplate material_template = {
        .ShaderBaseName        = "pbr",
        .SupportedPermutations = kAllTestPermutations,
        .PushConstantSize      = 0,
    };
    MaterialInstance material = {
        .Template           = &material_template,
        .ActivePermutations = ToMaterialPermutationMask(MaterialPermutation::AlphaTest) | ToMaterialPermutationMask(MaterialPermutation::AlphaBlend) | ToMaterialPermutationMask(MaterialPermutation::DoubleSided) | ToMaterialPermutationMask(MaterialPermutation::Clearcoat) | (UINT64_C(1) << 63),
    };

    const ShaderVariantKey lit = system.ResolveVariantKey(material, PassContext::Lit);
    EXPECT_EQ(lit.BaseShaderHash, HashMaterialShaderBaseName("pbr"));
    EXPECT_EQ(lit.PermutationMask, material.ActivePermutations & material_template.SupportedPermutations);

    const ShaderVariantKey depth = system.ResolveVariantKey(material, PassContext::DepthPrePass);
    EXPECT_EQ(depth.PermutationMask, ToMaterialPermutationMask(MaterialPermutation::AlphaTest) | ToMaterialPermutationMask(MaterialPermutation::DoubleSided));
    EXPECT_EQ(depth.Context, PassContext::DepthPrePass);
}

TEST(MaterialSystemTest, UsesAnExternalParameterBindingWhenPushConstantsDoNotFit)
{
    MaterialSystem system = {};
    system.Initialize(64);

    uint8_t          inline_parameters[32] = {};
    MaterialTemplate inline_template       = {
        .ShaderBaseName   = "inline",
        .PushConstantSize = sizeof(inline_parameters),
    };
    MaterialInstance inline_instance = {};
    EXPECT_TRUE(system.CreateInstance(&inline_template, 0, inline_parameters, sizeof(inline_parameters), kInvalidMaterialParameterIndex, &inline_instance));
    EXPECT_EQ(inline_instance.ParameterStorage, MaterialParameterStorage::InlinePushConstants);
    EXPECT_EQ(inline_instance.Parameters[0], 0u);

    MaterialTemplate external_template = {
        .ShaderBaseName   = "external",
        .PushConstantSize = 96,
    };
    MaterialInstance external_instance = {};
    EXPECT_FALSE(system.CreateInstance(&external_template, 0, nullptr, 0, kInvalidMaterialParameterIndex, &external_instance));
    EXPECT_TRUE(system.CreateInstance(&external_template, 0, nullptr, 0, 17, &external_instance));
    EXPECT_EQ(external_instance.ParameterStorage, MaterialParameterStorage::ExternalBuffer);
    EXPECT_EQ(external_instance.ParameterByteCount, 96u);
    EXPECT_EQ(external_instance.InlineParameterByteCount, sizeof(uint32_t));
    EXPECT_EQ(external_instance.ExternalParameterIndex, 17u);
    uint32_t copied_external_index = 0;
    ZEngine::Helpers::secure_memcpy(&copied_external_index, sizeof(copied_external_index), external_instance.Parameters, sizeof(copied_external_index));
    EXPECT_EQ(copied_external_index, 17u);
}

TEST(MaterialSystemTest, CollectsDeterministicUniqueVariantsWithinTheBudget)
{
    MaterialSystem system = {};
    system.Initialize(128);

    MaterialTemplate first_template = {
        .ShaderBaseName        = "first",
        .SupportedPermutations = kAllTestPermutations,
    };
    MaterialTemplate second_template = {
        .ShaderBaseName        = "second",
        .SupportedPermutations = kAllTestPermutations,
    };
    MaterialInstance materials[] = {
        { .Template = &first_template,   .ActivePermutations = ToMaterialPermutationMask(MaterialPermutation::DoubleSided)},
        { .Template = &first_template,   .ActivePermutations = ToMaterialPermutationMask(MaterialPermutation::DoubleSided)},
        {.Template = &second_template, .ActivePermutations = ToMaterialPermutationMask(MaterialPermutation::WithNormalMap)},
    };
    const PassContext      contexts[]  = {PassContext::Lit, PassContext::ShadowDepth};
    MaterialPrewarmRequest requests[4] = {};
    MaterialPrewarmResult  result      = {};
    const PrewarmBudget    budget      = {.MaxPermutations = 3};

    const uint32_t         count       = system.CollectPrewarmRequests({materials, sizeof(materials) / sizeof(materials[0])}, {contexts, sizeof(contexts) / sizeof(contexts[0])}, budget, requests, sizeof(requests) / sizeof(requests[0]), &result);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(result.CandidateCount, 6u);
    EXPECT_EQ(result.BudgetSkipped, 1u);
    EXPECT_EQ(requests[0].Template, &first_template);
    EXPECT_EQ(requests[0].Variant.Context, PassContext::Lit);
    EXPECT_EQ(requests[1].Variant.Context, PassContext::ShadowDepth);
    EXPECT_EQ(requests[2].Template, &second_template);
}

TEST(MaterialSystemTest, PrewarmReportsMissingAndRejectedRecipeProviders)
{
    MaterialSystem system = {};
    system.Initialize(128);

    MaterialTemplate  material_template = {.ShaderBaseName = "pbr"};
    MaterialInstance  material          = {.Template = &material_template};
    const PassContext context[]         = {PassContext::Lit};
    MemoryManager     manager           = {};
    manager.Initialize(ZMega(16), {});
    auto*                 cache  = ZPushStructCtor(&manager.MainArena, ZEngine::Rendering::Renderers::Pipelines::PSOCache);

    MaterialPrewarmResult result = system.PrewarmForMaterials({&material, 1}, {context, 1}, *cache, {});
    EXPECT_EQ(result.CandidateCount, 1u);
    EXPECT_EQ(result.RequestedCount, 0u);
    EXPECT_EQ(result.MissingRecipes, 1u);

    EXPECT_TRUE(system.RegisterGraphicsPrewarmRecipe(PassContext::Lit, {.BuildCreateInfo = &RejectPrewarmRecipe}));
    result = system.PrewarmForMaterials({&material, 1}, {context, 1}, *cache, {});
    EXPECT_EQ(result.RequestedCount, 0u);
    EXPECT_EQ(result.InvalidRecipes, 1u);
    EXPECT_FALSE(system.RegisterGraphicsPrewarmRecipe(PassContext::COUNT, {.BuildCreateInfo = &RejectPrewarmRecipe}));
    manager.Shutdown();
}

TEST(DrawSorterTest, SeparatesOpaqueAndTransparentSortContracts)
{
    MaterialTemplate opaque_template      = {.ShaderBaseName = "opaque"};
    MaterialTemplate transparent_template = {.ShaderBaseName = "transparent"};
    MaterialInstance opaque               = {.Template = &opaque_template};
    MaterialInstance transparent          = {
        .Template           = &transparent_template,
        .ActivePermutations = ToMaterialPermutationMask(MaterialPermutation::AlphaBlend),
    };
    const MaterialDrawItem draws[] = {
        {     .Material = &opaque, .PSOKeyHash = 5, .MaterialIndex = 2, .ViewDepth = 10.0f,  .SubmissionOrder = 0},
        {     .Material = &opaque, .PSOKeyHash = 3, .MaterialIndex = 5, .ViewDepth = 10.0f,  .SubmissionOrder = 1},
        {     .Material = &opaque, .PSOKeyHash = 3, .MaterialIndex = 2, .ViewDepth = 20.0f,  .SubmissionOrder = 2},
        {     .Material = &opaque, .PSOKeyHash = 3, .MaterialIndex = 2,  .ViewDepth = 2.0f,  .SubmissionOrder = 3},
        {.Material = &transparent, .PSOKeyHash = 1, .MaterialIndex = 1,  .ViewDepth = 2.0f, .SubmissionOrder = 14},
        {.Material = &transparent, .PSOKeyHash = 9, .MaterialIndex = 9, .ViewDepth = 15.0f, .SubmissionOrder = 13},
        {.Material = &transparent, .PSOKeyHash = 2, .MaterialIndex = 2, .ViewDepth = 15.0f, .SubmissionOrder = 12},
    };

    MemoryManager manager = {};
    manager.Initialize(16384, {});
    MaterialDrawLists lists = {};
    DrawSorter::Sort(&manager.MainArena, {draws, sizeof(draws) / sizeof(draws[0])}, &lists);

    ASSERT_EQ(lists.Opaque.size(), 4u);
    EXPECT_EQ(lists.Opaque[0].SubmissionOrder, 3u);
    EXPECT_EQ(lists.Opaque[1].SubmissionOrder, 2u);
    EXPECT_EQ(lists.Opaque[2].SubmissionOrder, 1u);
    EXPECT_EQ(lists.Opaque[3].SubmissionOrder, 0u);

    ASSERT_EQ(lists.Transparent.size(), 3u);
    EXPECT_EQ(lists.Transparent[0].SubmissionOrder, 12u);
    EXPECT_EQ(lists.Transparent[1].SubmissionOrder, 13u);
    EXPECT_EQ(lists.Transparent[2].SubmissionOrder, 14u);
    manager.Shutdown();
}

TEST(DrawSorterTest, UsesSubmissionOrderForEqualAndInvalidTransparentDepth)
{
    MaterialTemplate transparent_template = {.ShaderBaseName = "transparent"};
    MaterialInstance transparent          = {
        .Template           = &transparent_template,
        .ActivePermutations = ToMaterialPermutationMask(MaterialPermutation::AlphaBlend),
    };
    const MaterialDrawItem draws[] = {
        {.Material = &transparent, .PSOKeyHash = 7, .MaterialIndex = 7, .ViewDepth = std::numeric_limits<float>::quiet_NaN(), .SubmissionOrder = 5},
        {.Material = &transparent, .PSOKeyHash = 1, .MaterialIndex = 1, .ViewDepth = std::numeric_limits<float>::quiet_NaN(), .SubmissionOrder = 3},
    };

    MemoryManager manager = {};
    manager.Initialize(16384, {});
    MaterialDrawLists lists = {};
    DrawSorter::Sort(&manager.MainArena, {draws, sizeof(draws) / sizeof(draws[0])}, &lists);

    ASSERT_EQ(lists.Transparent.size(), 2u);
    EXPECT_EQ(lists.Transparent[0].SubmissionOrder, 3u);
    EXPECT_EQ(lists.Transparent[1].SubmissionOrder, 5u);
    manager.Shutdown();
}
