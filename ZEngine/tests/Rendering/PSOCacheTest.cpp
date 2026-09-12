#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Rendering::Renderers::Pipelines;

namespace
{
    bool RemoveMatchingKey(void* context, const uint32_t& key, uint32_t&)
    {
        return key == *static_cast<const uint32_t*>(context);
    }

    struct PSOCacheTestStorage
    {
        MemoryManager Manager = {};
        PSOCache*     Cache   = nullptr;

        PSOCacheTestStorage()
        {
            Manager.Initialize(ZMega(16), {});
            Cache = ZPushStructCtor(&Manager.MainArena, PSOCache);
        }

        ~PSOCacheTestStorage()
        {
            Manager.Shutdown();
        }

        PSOCache& Get()
        {
            return *Cache;
        }
    };
} // namespace

TEST(PSOCacheCollisionBucketsTest, KeepsDistinctKeysInTheSameHashBucket)
{
    PSOCollisionBuckets<uint32_t, uint32_t, 1, 2> buckets = {};
    bool                                          created = false;

    auto*                                         first   = buckets.FindOrInsert(7, 11, &created);
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(created);
    *first       = 101;

    auto* second = buckets.FindOrInsert(7, 22, &created);
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(created);
    *second = 202;

    EXPECT_EQ(buckets.Find(7, 11), first);
    EXPECT_EQ(buckets.Find(7, 22), second);
    EXPECT_EQ(*buckets.Find(7, 11), 101u);
    EXPECT_EQ(*buckets.Find(7, 22), 202u);
}

TEST(PSOCacheCollisionBucketsTest, ReportsBucketCapacityWithoutOverwritingAnEntry)
{
    PSOCollisionBuckets<uint32_t, uint32_t, 1, 2> buckets = {};
    bool                                          created = false;

    ASSERT_NE(buckets.FindOrInsert(9, 1, &created), nullptr);
    ASSERT_NE(buckets.FindOrInsert(9, 2, &created), nullptr);
    EXPECT_EQ(buckets.FindOrInsert(9, 3, &created), nullptr);
    EXPECT_FALSE(created);
    EXPECT_NE(buckets.Find(9, 1), nullptr);
    EXPECT_NE(buckets.Find(9, 2), nullptr);
}

TEST(PSOCacheCollisionBucketsTest, RemovesOnlyEntriesSelectedByThePredicate)
{
    PSOCollisionBuckets<uint32_t, uint32_t, 1, 3> buckets = {};
    bool                                          created = false;

    *buckets.FindOrInsert(5, 11, &created)                = 101;
    *buckets.FindOrInsert(5, 22, &created)                = 202;
    *buckets.FindOrInsert(5, 33, &created)                = 303;

    uint32_t removed_key                                  = 22;
    buckets.RemoveIf(&removed_key, &RemoveMatchingKey);

    EXPECT_NE(buckets.Find(5, 11), nullptr);
    EXPECT_EQ(buckets.Find(5, 22), nullptr);
    EXPECT_NE(buckets.Find(5, 33), nullptr);
    EXPECT_EQ(*buckets.Find(5, 11), 101u);
    EXPECT_EQ(*buckets.Find(5, 33), 303u);
}

TEST(PSOCacheKeyTest, CanonicalizesDescriptorBindingsAndTheirFlags)
{
    PSOCacheTestStorage          storage           = {};
    PSOCache&                    cache             = storage.Get();
    VkDescriptorSetLayoutBinding first_bindings[2] = {
        {.binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2,   .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
    };
    VkDescriptorBindingFlags first_flags[2] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        0,
    };
    VkDescriptorSetLayoutBindingFlagsCreateInfo first_flags_info = {};
    first_flags_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    first_flags_info.bindingCount                                = 2;
    first_flags_info.pBindingFlags                               = first_flags;
    VkDescriptorSetLayoutCreateInfo first                        = {};
    first.sType                                                  = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    first.pNext                                                  = &first_flags_info;
    first.bindingCount                                           = 2;
    first.pBindings                                              = first_bindings;

    VkDescriptorSetLayoutBinding second_bindings[2]              = {
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2,   .stageFlags = VK_SHADER_STAGE_VERTEX_BIT},
        {.binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
    };
    VkDescriptorBindingFlags second_flags[2] = {
        0,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
    };
    VkDescriptorSetLayoutBindingFlagsCreateInfo second_flags_info = {};
    second_flags_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    second_flags_info.bindingCount                                = 2;
    second_flags_info.pBindingFlags                               = second_flags;
    VkDescriptorSetLayoutCreateInfo second                        = {};
    second.sType                                                  = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    second.pNext                                                  = &second_flags_info;
    second.bindingCount                                           = 2;
    second.pBindings                                              = second_bindings;

    const auto first_key                                          = cache.MakeDescriptorSetLayoutKey(first);
    const auto second_key                                         = cache.MakeDescriptorSetLayoutKey(second);
    EXPECT_TRUE(first_key == second_key);
    EXPECT_EQ(first_key.Bindings[0].Binding, 1u);
    EXPECT_EQ(first_key.Bindings[1].Binding, 4u);
    EXPECT_EQ(first_key.Bindings[1].BindingFlags, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
}

TEST(PSOCacheKeyTest, IncludesOrderedImmutableSamplerHandles)
{
    PSOCacheTestStorage storage     = {};
    PSOCache&           cache       = storage.Get();
    VkSampler           samplers[2] = {
        reinterpret_cast<VkSampler>(uintptr_t(3)),
        reinterpret_cast<VkSampler>(uintptr_t(4)),
    };
    VkDescriptorSetLayoutBinding binding = {
        .binding            = 0,
        .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount    = 2,
        .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = samplers,
    };
    VkDescriptorSetLayoutCreateInfo create_info = {};
    create_info.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount                    = 1;
    create_info.pBindings                       = &binding;

    const PSODescriptorSetLayoutKey first_key   = cache.MakeDescriptorSetLayoutKey(create_info);
    const PSODescriptorSetLayoutKey same_key    = cache.MakeDescriptorSetLayoutKey(create_info);
    EXPECT_TRUE(first_key == same_key);

    samplers[1]                                   = reinterpret_cast<VkSampler>(uintptr_t(5));
    const PSODescriptorSetLayoutKey different_key = cache.MakeDescriptorSetLayoutKey(create_info);
    EXPECT_FALSE(first_key == different_key);
}

TEST(PSOCacheKeyTest, IncludesEverySupportedSamplerCreationField)
{
    VkSamplerCreateInfo first  = {};
    first.sType                = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    first.magFilter            = VK_FILTER_LINEAR;
    first.minFilter            = VK_FILTER_NEAREST;
    first.mipmapMode           = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    first.addressModeU         = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    first.addressModeV         = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    first.addressModeW         = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    first.mipLodBias           = 0.5f;
    first.anisotropyEnable     = VK_TRUE;
    first.maxAnisotropy        = 8.0f;
    first.compareEnable        = VK_TRUE;
    first.compareOp            = VK_COMPARE_OP_LESS_OR_EQUAL;
    first.minLod               = 1.0f;
    first.maxLod               = 4.0f;
    first.borderColor          = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VkSamplerCreateInfo second = first;
    EXPECT_TRUE(PSOCache::MakeSamplerKey(first) == PSOCache::MakeSamplerKey(second));

    second.maxLod = 5.0f;
    EXPECT_FALSE(PSOCache::MakeSamplerKey(first) == PSOCache::MakeSamplerKey(second));
}

TEST(PSOCacheKeyTest, PinsComputePipelineToItsShaderGeneration)
{
    PSOCacheTestStorage         storage     = {};
    PSOCache&                   cache       = storage.Get();

    VkComputePipelineCreateInfo create_info = {};
    create_info.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    create_info.stage.sType                 = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    create_info.stage.stage                 = VK_SHADER_STAGE_COMPUTE_BIT;
    create_info.stage.module                = reinterpret_cast<VkShaderModule>(uintptr_t(1));
    create_info.stage.pName                 = "main";
    create_info.layout                      = reinterpret_cast<VkPipelineLayout>(uintptr_t(2));
    create_info.basePipelineIndex           = -1;

    const auto first_generation             = cache.MakeComputePipelineKey(create_info, 4);
    const auto second_generation            = cache.MakeComputePipelineKey(create_info, 5);
    EXPECT_FALSE(first_generation == second_generation);
}

TEST(PSOCacheKeyTest, CanonicalizesSpecializationValuesByConstantId)
{
    PSOCacheTestStorage      storage          = {};
    PSOCache&                cache            = storage.Get();
    uint32_t                 first_data[2]    = {17, 29};
    VkSpecializationMapEntry first_entries[2] = {
        {.constantID = 7,                .offset = 0, .size = sizeof(uint32_t)},
        {.constantID = 3, .offset = sizeof(uint32_t), .size = sizeof(uint32_t)},
    };
    VkSpecializationInfo first_specialization = {
        .mapEntryCount = 2,
        .pMapEntries   = first_entries,
        .dataSize      = sizeof(first_data),
        .pData         = first_data,
    };

    VkComputePipelineCreateInfo create_info          = {};
    create_info.sType                                = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    create_info.stage.sType                          = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    create_info.stage.stage                          = VK_SHADER_STAGE_COMPUTE_BIT;
    create_info.stage.module                         = reinterpret_cast<VkShaderModule>(uintptr_t(1));
    create_info.stage.pName                          = "main";
    create_info.stage.pSpecializationInfo            = &first_specialization;
    create_info.layout                               = reinterpret_cast<VkPipelineLayout>(uintptr_t(2));
    create_info.basePipelineIndex                    = -1;

    const PSOComputePipelineKey first_key            = cache.MakeComputePipelineKey(create_info, 0);

    uint32_t                    reordered_data[2]    = {29, 17};
    VkSpecializationMapEntry    reordered_entries[2] = {
        {.constantID = 3,                .offset = 0, .size = sizeof(uint32_t)},
        {.constantID = 7, .offset = sizeof(uint32_t), .size = sizeof(uint32_t)},
    };
    VkSpecializationInfo reordered_specialization = {
        .mapEntryCount = 2,
        .pMapEntries   = reordered_entries,
        .dataSize      = sizeof(reordered_data),
        .pData         = reordered_data,
    };
    create_info.stage.pSpecializationInfo     = &reordered_specialization;
    const PSOComputePipelineKey reordered_key = cache.MakeComputePipelineKey(create_info, 0);
    EXPECT_TRUE(first_key == reordered_key);

    reordered_data[1]                           = 30;
    const PSOComputePipelineKey different_value = cache.MakeComputePipelineKey(create_info, 0);
    EXPECT_FALSE(first_key == different_value);
}

TEST(PSOCacheKeyTest, CanonicalizesGraphicsFixedStateAndPinsShaderGeneration)
{
    PSOCacheTestStorage             storage     = {};
    PSOCache&                       cache       = storage.Get();
    VkPipelineShaderStageCreateInfo stages[2]   = {};
    stages[0].sType                             = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage                             = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module                            = reinterpret_cast<VkShaderModule>(uintptr_t(3));
    stages[0].pName                             = "main";
    stages[1].sType                             = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage                             = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module                            = reinterpret_cast<VkShaderModule>(uintptr_t(4));
    stages[1].pName                             = "main";

    VkVertexInputBindingDescription bindings[1] = {
        {.binding = 3, .stride = 24, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}
    };
    VkVertexInputAttributeDescription attributes[1] = {
        {.location = 2, .binding = 3, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0}
    };
    VkPipelineVertexInputStateCreateInfo vertex_input     = {};
    vertex_input.sType                                    = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount            = 1;
    vertex_input.pVertexBindingDescriptions               = bindings;
    vertex_input.vertexAttributeDescriptionCount          = 1;
    vertex_input.pVertexAttributeDescriptions             = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType                                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology                               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport            = {};
    viewport.sType                                        = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount                                = 1;
    viewport.scissorCount                                 = 1;
    VkPipelineRasterizationStateCreateInfo rasterization  = {};
    rasterization.sType                                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode                             = VK_POLYGON_MODE_FILL;
    rasterization.cullMode                                = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace                               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth                               = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample      = {};
    multisample.sType                                     = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples                      = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_stencil   = {};
    depth_stencil.sType                                   = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthCompareOp                          = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState color_attachment  = {};
    color_attachment.colorWriteMask                       = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend       = {};
    color_blend.sType                                     = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.logicOp                                   = VK_LOGIC_OP_COPY;
    color_blend.attachmentCount                           = 1;
    color_blend.pAttachments                              = &color_attachment;
    VkDynamicState                   dynamic_states[2]    = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state        = {};
    dynamic_state.sType                                   = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount                       = 2;
    dynamic_state.pDynamicStates                          = dynamic_states;

    VkGraphicsPipelineCreateInfo create_info              = {};
    create_info.sType                                     = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount                                = 2;
    create_info.pStages                                   = stages;
    create_info.pVertexInputState                         = &vertex_input;
    create_info.pInputAssemblyState                       = &input_assembly;
    create_info.pViewportState                            = &viewport;
    create_info.pRasterizationState                       = &rasterization;
    create_info.pMultisampleState                         = &multisample;
    create_info.pDepthStencilState                        = &depth_stencil;
    create_info.pColorBlendState                          = &color_blend;
    create_info.pDynamicState                             = &dynamic_state;
    create_info.layout                                    = reinterpret_cast<VkPipelineLayout>(uintptr_t(5));
    create_info.renderPass                                = reinterpret_cast<VkRenderPass>(uintptr_t(6));
    create_info.basePipelineIndex                         = -1;

    const auto                      first_key             = cache.MakeGraphicsPipelineKey(create_info, 7);
    VkPipelineShaderStageCreateInfo reverse_stages[2]     = {stages[1], stages[0]};
    create_info.pStages                                   = reverse_stages;
    const auto reordered_key                              = cache.MakeGraphicsPipelineKey(create_info, 7);
    EXPECT_TRUE(first_key == reordered_key);

    const auto next_generation = cache.MakeGraphicsPipelineKey(create_info, 8);
    EXPECT_FALSE(first_key == next_generation);

    rasterization.cullMode             = VK_CULL_MODE_FRONT_BIT;
    const auto different_rasterization = cache.MakeGraphicsPipelineKey(create_info, 7);
    EXPECT_FALSE(first_key == different_rasterization);

    VkFormat                      dynamic_formats[1] = {VK_FORMAT_R8G8B8A8_UNORM};
    VkPipelineRenderingCreateInfo dynamic_rendering  = {};
    dynamic_rendering.sType                          = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    dynamic_rendering.viewMask                       = 0;
    dynamic_rendering.colorAttachmentCount           = 1;
    dynamic_rendering.pColorAttachmentFormats        = dynamic_formats;
    dynamic_rendering.depthAttachmentFormat          = VK_FORMAT_D32_SFLOAT;

    create_info.renderPass                           = VK_NULL_HANDLE;
    create_info.subpass                              = 0;
    create_info.pNext                                = &dynamic_rendering;
    const auto dynamic_key                           = cache.MakeGraphicsPipelineKey(create_info, 7);
    EXPECT_TRUE(dynamic_key.UsesDynamicRendering);

    dynamic_rendering.viewMask     = 1;
    const auto different_view_mask = cache.MakeGraphicsPipelineKey(create_info, 7);
    EXPECT_FALSE(dynamic_key == different_view_mask);

    dynamic_rendering.viewMask  = 0;
    dynamic_formats[0]          = VK_FORMAT_R16G16B16A16_SFLOAT;
    const auto different_format = cache.MakeGraphicsPipelineKey(create_info, 7);
    EXPECT_FALSE(dynamic_key == different_format);

    create_info.pNext      = nullptr;
    create_info.renderPass = reinterpret_cast<VkRenderPass>(uintptr_t(6));
    const auto legacy_key  = cache.MakeGraphicsPipelineKey(create_info, 7);
    EXPECT_FALSE(dynamic_key == legacy_key);
}
