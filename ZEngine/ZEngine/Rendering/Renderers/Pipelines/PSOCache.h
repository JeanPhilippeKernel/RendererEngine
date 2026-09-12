#pragma once
#include <ZEngine/Core/Containers/MPSCQueue.h>
#include <ZEngine/ZEngineDef.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>

namespace ZEngine::Hardwares
{
    struct VulkanDevice;
}

namespace ZEngine::Core::VFS
{
    struct IVFSContext;
}

namespace ZEngine::Rendering::Renderers::RenderPasses
{
    struct Attachment;
}

namespace ZEngine::Rendering::Renderers::Pipelines
{
    /// @brief Engine-supported descriptor kinds used in canonical layout keys.
    enum class PSODescriptorKind : uint8_t
    {
        Sampler = 0,
        CombinedImageSampler,
        SampledImage,
        StorageImage,
        UniformTexelBuffer,
        StorageTexelBuffer,
        UniformBuffer,
        StorageBuffer,
        UniformBufferDynamic,
        StorageBufferDynamic,
        InputAttachment,
    };

    /// @brief Engine-supported sampler filters used in canonical sampler keys.
    enum class PSOFilter : uint8_t
    {
        Nearest = 0,
        Linear,
    };

    /// @brief Engine-supported sampler mip modes used in canonical sampler keys.
    enum class PSOSamplerMipmapMode : uint8_t
    {
        Nearest = 0,
        Linear,
    };

    /// @brief Engine-supported sampler address modes used in canonical sampler keys.
    enum class PSOSamplerAddressMode : uint8_t
    {
        Repeat = 0,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
        MirrorClampToEdge,
    };

    /// @brief Engine-supported comparison operations used in canonical sampler keys.
    enum class PSOCompareOp : uint8_t
    {
        Never = 0,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    /// @brief Engine-supported border colors used in canonical sampler keys.
    enum class PSOBorderColor : uint8_t
    {
        FloatTransparentBlack = 0,
        IntTransparentBlack,
        FloatOpaqueBlack,
        IntOpaqueBlack,
        FloatOpaqueWhite,
        IntOpaqueWhite,
    };

    /// @brief Engine-supported shader stages used by synchronous PSO keys.
    enum class PSOShaderStage : uint8_t
    {
        Compute = 0,
    };

    /// @brief Fixed collision buckets whose lookup always checks complete key equality.
    template <typename Key, typename Value, uint32_t BucketCount, uint32_t EntriesPerBucket>
    class PSOCollisionBuckets
    {
    public:
        struct Entry
        {
            Key   Identity = {};
            Value Data     = {};
            bool  Occupied = false;
        };

        Value* Find(uint64_t hash, const Key& key)
        {
            Entry* bucket = m_buckets[hash % BucketCount];
            for (uint32_t i = 0; i < EntriesPerBucket; ++i)
            {
                if (bucket[i].Occupied && bucket[i].Identity == key)
                    return &bucket[i].Data;
            }
            return nullptr;
        }

        const Value* Find(uint64_t hash, const Key& key) const
        {
            const Entry* bucket = m_buckets[hash % BucketCount];
            for (uint32_t i = 0; i < EntriesPerBucket; ++i)
            {
                if (bucket[i].Occupied && bucket[i].Identity == key)
                    return &bucket[i].Data;
            }
            return nullptr;
        }

        /// @brief Returns an existing value or reserves a free entry in the selected bucket.
        Value* FindOrInsert(uint64_t hash, const Key& key, bool* created)
        {
            ZENGINE_VALIDATE_ASSERT(created != nullptr, "PSOCollisionBuckets::FindOrInsert requires a created flag")
            if (Value* found = Find(hash, key))
            {
                *created = false;
                return found;
            }

            Entry* bucket = m_buckets[hash % BucketCount];
            for (uint32_t i = 0; i < EntriesPerBucket; ++i)
            {
                if (!bucket[i].Occupied)
                {
                    bucket[i].Identity = key;
                    bucket[i].Data     = {};
                    bucket[i].Occupied = true;
                    *created           = true;
                    return &bucket[i].Data;
                }
            }

            *created = false;
            return nullptr;
        }

        using Visitor         = void (*)(void* context, Value& value);
        using ConstVisitor    = void (*)(void* context, const Value& value);
        using RemovePredicate = bool (*)(void* context, const Key& key, Value& value);

        /// @brief Visits each occupied entry with an explicit caller-owned context.
        void ForEach(void* context, Visitor visitor)
        {
            ZENGINE_VALIDATE_ASSERT(visitor != nullptr, "PSOCollisionBuckets::ForEach requires a visitor")
            for (uint32_t bucket_index = 0; bucket_index < BucketCount; ++bucket_index)
            {
                for (uint32_t entry_index = 0; entry_index < EntriesPerBucket; ++entry_index)
                {
                    Entry& entry = m_buckets[bucket_index][entry_index];
                    if (entry.Occupied)
                        visitor(context, entry.Data);
                }
            }
        }

        /// @brief Visits each occupied entry through a const callback and explicit caller-owned context.
        void ForEach(void* context, ConstVisitor visitor) const
        {
            ZENGINE_VALIDATE_ASSERT(visitor != nullptr, "PSOCollisionBuckets::ForEach requires a visitor")
            for (uint32_t bucket_index = 0; bucket_index < BucketCount; ++bucket_index)
            {
                for (uint32_t entry_index = 0; entry_index < EntriesPerBucket; ++entry_index)
                {
                    const Entry& entry = m_buckets[bucket_index][entry_index];
                    if (entry.Occupied)
                        visitor(context, entry.Data);
                }
            }
        }

        /// @brief Removes each matching entry without disturbing other collision entries.
        void RemoveIf(void* context, RemovePredicate predicate)
        {
            ZENGINE_VALIDATE_ASSERT(predicate != nullptr, "PSOCollisionBuckets::RemoveIf requires a predicate")
            for (uint32_t bucket_index = 0; bucket_index < BucketCount; ++bucket_index)
            {
                for (uint32_t entry_index = 0; entry_index < EntriesPerBucket; ++entry_index)
                {
                    Entry& entry = m_buckets[bucket_index][entry_index];
                    if (entry.Occupied && predicate(context, entry.Identity, entry.Data))
                        entry = {};
                }
            }
        }

        /// @brief Removes one exact entry from the selected collision bucket.
        bool Erase(uint64_t hash, const Key& key)
        {
            Entry* bucket = m_buckets[hash % BucketCount];
            for (uint32_t i = 0; i < EntriesPerBucket; ++i)
            {
                if (bucket[i].Occupied && bucket[i].Identity == key)
                {
                    bucket[i] = {};
                    return true;
                }
            }
            return false;
        }

        /// @brief Removes entries after their owned Vulkan handles have been destroyed.
        void Clear()
        {
            for (uint32_t bucket_index = 0; bucket_index < BucketCount; ++bucket_index)
            {
                for (uint32_t entry_index = 0; entry_index < EntriesPerBucket; ++entry_index)
                    m_buckets[bucket_index][entry_index] = {};
            }
        }

    private:
        Entry m_buckets[BucketCount][EntriesPerBucket] = {};
    };

    inline constexpr uint32_t kMaxPSOLayoutBindings                       = 32;
    inline constexpr uint32_t kMaxPSOSetLayouts                           = 8;
    inline constexpr uint32_t kMaxPSOPushConstantRanges                   = 8;
    inline constexpr uint32_t kMaxPSOCompatibilityAttachments             = 8;
    inline constexpr uint32_t kMaxPSOGraphicsShaderStages                 = 5;
    inline constexpr uint32_t kMaxPSOSpecializationEntries                = 8;
    inline constexpr uint32_t kMaxPSOSpecializationValueBytes             = 64;
    inline constexpr uint32_t kMaxPSOImmutableSamplers                    = 16;
    inline constexpr uint32_t kMaxPSOVertexBindings                       = 16;
    inline constexpr uint32_t kMaxPSOVertexAttributes                     = 32;
    inline constexpr uint32_t kMaxPSOColorBlendAttachments                = 8;
    inline constexpr uint32_t kPSOSamplerBucketCount                      = 64;
    inline constexpr uint32_t kPSOSamplerEntriesPerBucket                 = 4;
    inline constexpr uint32_t kPSOImmutableSamplerListBucketCount         = 64;
    inline constexpr uint32_t kPSOImmutableSamplerListEntriesPerBucket    = 4;
    inline constexpr uint32_t kPSOLayoutBucketCount                       = 128;
    inline constexpr uint32_t kPSOLayoutEntriesPerBucket                  = 4;
    inline constexpr uint32_t kPSOPipelineLayoutBucketCount               = 128;
    inline constexpr uint32_t kPSOPipelineLayoutEntriesPerBucket          = 4;
    inline constexpr uint32_t kPSOCompatibilityRenderPassBucketCount      = 128;
    inline constexpr uint32_t kPSOCompatibilityRenderPassEntriesPerBucket = 4;
    inline constexpr uint32_t kPSOComputePipelineBucketCount              = 128;
    inline constexpr uint32_t kPSOComputePipelineEntriesPerBucket         = 4;
    inline constexpr uint32_t kPSOGraphicsPipelineBucketCount             = 256;
    inline constexpr uint32_t kPSOGraphicsPipelineEntriesPerBucket        = 4;
    inline constexpr uint64_t kPSOPipelineEvictionAge                     = 300;
    /// @brief Matches one full bounded material-prewarm submission.
    inline constexpr uint32_t kMaxPSOWarmupRecipes                        = 256;
    inline constexpr uint32_t kMaxPSOWarmupShaderNameBytes                = 96;
    inline constexpr uint32_t kMaxPSOAsyncJobs                            = 256;
    inline constexpr uint32_t kMaxPSOAsyncWaiters                         = 8;
    inline constexpr uint32_t kMaxPSOWorkerPipelineCaches                 = 4;
    /// @brief Matches async-job capacity during mass permutation hot reload.
    inline constexpr uint32_t kMaxPSORetiredShaderModules                 = 256;

    /// @brief Render-thread-owned lifetime state for a cached pipeline entry.
    enum class PSOPipelineState : uint8_t
    {
        Compiling = 0,
        Ready,
        Retiring,
        Discarded,
    };

    /// @brief C-style callback notified after an asynchronous pipeline request resolves.
    using PSOPipelineReadyFn = void (*)(void* context, VkPipeline pipeline);

    /// @brief Caller-owned asynchronous pipeline notification.
    struct PSOPipelineWaiter
    {
        void*              Context  = nullptr;
        PSOPipelineReadyFn Callback = nullptr;
    };

    /// @brief Fixed-size sampler identity with bit-exact floating-point fields.
    struct PSOSamplerKey
    {
        uint32_t              CreateFlags        = 0;
        uint32_t              MipLodBiasBits     = 0;
        uint32_t              MaxAnisotropyBits  = 0;
        uint32_t              MinLodBits         = 0;
        uint32_t              MaxLodBits         = 0;
        PSOCompareOp          CompareOp          = PSOCompareOp::Never;
        PSOBorderColor        BorderColor        = PSOBorderColor::FloatTransparentBlack;
        PSOFilter             MagFilter          = PSOFilter::Nearest;
        PSOFilter             MinFilter          = PSOFilter::Nearest;
        PSOSamplerMipmapMode  MipmapMode         = PSOSamplerMipmapMode::Nearest;
        PSOSamplerAddressMode AddressModeU       = PSOSamplerAddressMode::Repeat;
        PSOSamplerAddressMode AddressModeV       = PSOSamplerAddressMode::Repeat;
        PSOSamplerAddressMode AddressModeW       = PSOSamplerAddressMode::Repeat;
        uint8_t               AnisotropyEnabled  = 0;
        uint8_t               CompareEnabled     = 0;
        uint8_t               UnnormalizedCoords = 0;
        uint8_t               Reserved           = 0;

        bool                  operator==(const PSOSamplerKey& other) const;
    };

    /// @brief Ordered immutable sampler handles used by one descriptor-set layout binding.
    struct PSOImmutableSamplerListKey
    {
        VkSampler Samplers[kMaxPSOImmutableSamplers] = {};
        uint32_t  SamplerCount                       = 0;
        uint32_t  Reserved                           = 0;

        bool      operator==(const PSOImmutableSamplerListKey& other) const;
    };
    static_assert(sizeof(PSOImmutableSamplerListKey) == sizeof(VkSampler) * kMaxPSOImmutableSamplers + sizeof(uint64_t));

    /// @brief Canonical attachment state required for render-pass compatibility.
    struct PSOCompatibilityAttachmentKey
    {
        uint32_t Format          = VK_FORMAT_UNDEFINED;
        uint32_t Samples         = VK_SAMPLE_COUNT_1_BIT;
        uint32_t ReferenceLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        bool     operator==(const PSOCompatibilityAttachmentKey& other) const;
    };

    /// @brief Fixed-size key for render passes used exclusively during graphics-pipeline creation.
    struct PSOCompatibilityRenderPassKey
    {
        uint32_t                      AttachmentCount                              = 0;
        uint32_t                      ColorAttachmentCount                         = 0;
        uint32_t                      DepthAttachmentIndex                         = VK_ATTACHMENT_UNUSED;
        uint32_t                      ViewMask                                     = 0;
        PSOCompatibilityAttachmentKey Attachments[kMaxPSOCompatibilityAttachments] = {};

        bool                          operator==(const PSOCompatibilityRenderPassKey& other) const;
    };

    /// @brief A cached pipeline-layout ID or a complete external Vulkan-handle identity.
    struct PSOPipelineLayoutReference
    {
        uint64_t         CachedIdentity = 0;
        VkPipelineLayout ExternalHandle = VK_NULL_HANDLE;

        bool             operator==(const PSOPipelineLayoutReference& other) const;
    };

    /// @brief One canonical specialization-constant value, identified independently of source byte layout.
    struct PSOSpecializationValueKey
    {
        uint32_t ConstantId                            = 0;
        uint32_t ByteCount                             = 0;
        uint8_t  Data[kMaxPSOSpecializationValueBytes] = {};

        bool     operator==(const PSOSpecializationValueKey& other) const;
    };

    /// @brief Fixed-capacity, canonical specialization input for one shader stage.
    struct PSOSpecializationKey
    {
        uint32_t                  EntryCount                            = 0;
        PSOSpecializationValueKey Entries[kMaxPSOSpecializationEntries] = {};

        bool                      operator==(const PSOSpecializationKey& other) const;
    };

    /// @brief Canonical compute stage identity pinned to one shader module generation.
    struct PSOComputeShaderKey
    {
        uint64_t             ModuleIdentity = 0;
        uint32_t             Generation     = 0;
        PSOShaderStage       Stage          = PSOShaderStage::Compute;
        uint8_t              Reserved[3]    = {};
        PSOSpecializationKey Specialization = {};
        uint32_t             ReservedTail   = 0;

        bool                 operator==(const PSOComputeShaderKey& other) const;
    };
    static_assert(sizeof(PSOComputeShaderKey) == sizeof(uint64_t) + sizeof(uint32_t) + sizeof(PSOShaderStage) + 3 + sizeof(PSOSpecializationKey) + sizeof(uint32_t));

    /// @brief Fixed-size compute pipeline identity for the synchronous cache path.
    struct PSOComputePipelineKey
    {
        PSOComputeShaderKey        Shader      = {};
        PSOPipelineLayoutReference Layout      = {};
        uint32_t                   CreateFlags = 0;
        uint32_t                   Reserved    = 0;

        bool                       operator==(const PSOComputePipelineKey& other) const;
    };
    static_assert(sizeof(PSOComputePipelineKey) == sizeof(PSOComputeShaderKey) + sizeof(PSOPipelineLayoutReference) + sizeof(uint64_t));

    /// @brief A cached compatibility render-pass ID or a complete external Vulkan-handle identity.
    struct PSORenderPassReference
    {
        uint64_t     CachedIdentity = 0;
        VkRenderPass ExternalHandle = VK_NULL_HANDLE;

        bool         operator==(const PSORenderPassReference& other) const;
    };

    /// @brief Canonical identity of one graphics shader stage.
    struct PSOGraphicsShaderStageKey
    {
        uint64_t             ModuleIdentity = 0;
        uint32_t             Generation     = 0;
        uint32_t             Stage          = 0;
        PSOSpecializationKey Specialization = {};
        uint32_t             Reserved       = 0;

        bool                 operator==(const PSOGraphicsShaderStageKey& other) const;
    };
    static_assert(sizeof(PSOGraphicsShaderStageKey) == sizeof(uint64_t) + sizeof(uint32_t) * 2 + sizeof(PSOSpecializationKey) + sizeof(uint32_t));

    /// @brief Canonical vertex-buffer binding state.
    struct PSOVertexBindingKey
    {
        uint32_t Binding   = 0;
        uint32_t Stride    = 0;
        uint32_t InputRate = 0;

        bool     operator==(const PSOVertexBindingKey& other) const;
    };

    /// @brief Canonical vertex attribute state.
    struct PSOVertexAttributeKey
    {
        uint32_t Location = 0;
        uint32_t Binding  = 0;
        uint32_t Format   = VK_FORMAT_UNDEFINED;
        uint32_t Offset   = 0;

        bool     operator==(const PSOVertexAttributeKey& other) const;
    };

    /// @brief Canonical stencil-operation state.
    struct PSOStencilOpStateKey
    {
        uint32_t FailOp      = 0;
        uint32_t PassOp      = 0;
        uint32_t DepthFailOp = 0;
        uint32_t CompareOp   = 0;
        uint32_t CompareMask = 0;
        uint32_t WriteMask   = 0;
        uint32_t Reference   = 0;

        bool     operator==(const PSOStencilOpStateKey& other) const;
    };

    /// @brief Canonical color-blend attachment state.
    struct PSOColorBlendAttachmentKey
    {
        uint32_t BlendEnable         = 0;
        uint32_t SrcColorBlendFactor = 0;
        uint32_t DstColorBlendFactor = 0;
        uint32_t ColorBlendOp        = 0;
        uint32_t SrcAlphaBlendFactor = 0;
        uint32_t DstAlphaBlendFactor = 0;
        uint32_t AlphaBlendOp        = 0;
        uint32_t ColorWriteMask      = 0;

        bool     operator==(const PSOColorBlendAttachmentKey& other) const;
    };

    /// @brief Complete fixed-function identity for a graphics pipeline with dynamic viewport and scissor.
    struct PSOGraphicsPipelineKey
    {
        uint32_t                   CreateFlags                                                   = 0;
        uint32_t                   ShaderStageCount                                              = 0;
        uint32_t                   Subpass                                                       = 0;
        uint32_t                   VertexBindingCount                                            = 0;
        uint32_t                   VertexAttributeCount                                          = 0;
        uint32_t                   InputAssemblyTopology                                         = 0;
        uint32_t                   PrimitiveRestartEnable                                        = 0;
        uint32_t                   ViewportCount                                                 = 0;
        uint32_t                   ScissorCount                                                  = 0;
        uint32_t                   RasterizationFlags                                            = 0;
        uint32_t                   DepthClampEnable                                              = 0;
        uint32_t                   RasterizerDiscardEnable                                       = 0;
        uint32_t                   PolygonMode                                                   = 0;
        uint32_t                   CullMode                                                      = 0;
        uint32_t                   FrontFace                                                     = 0;
        uint32_t                   DepthBiasEnable                                               = 0;
        uint32_t                   DepthBiasConstantFactorBits                                   = 0;
        uint32_t                   DepthBiasClampBits                                            = 0;
        uint32_t                   DepthBiasSlopeFactorBits                                      = 0;
        uint32_t                   LineWidthBits                                                 = 0;
        uint32_t                   MultisampleFlags                                              = 0;
        uint32_t                   RasterizationSamples                                          = 0;
        uint32_t                   SampleShadingEnable                                           = 0;
        uint32_t                   MinSampleShadingBits                                          = 0;
        uint32_t                   AlphaToCoverageEnable                                         = 0;
        uint32_t                   AlphaToOneEnable                                              = 0;
        uint32_t                   HasDepthStencilState                                          = 0;
        uint32_t                   DepthStencilFlags                                             = 0;
        uint32_t                   DepthTestEnable                                               = 0;
        uint32_t                   DepthWriteEnable                                              = 0;
        uint32_t                   DepthCompareOp                                                = 0;
        uint32_t                   DepthBoundsTestEnable                                         = 0;
        uint32_t                   StencilTestEnable                                             = 0;
        uint32_t                   MinDepthBoundsBits                                            = 0;
        uint32_t                   MaxDepthBoundsBits                                            = 0;
        uint32_t                   ColorBlendFlags                                               = 0;
        uint32_t                   LogicOpEnable                                                 = 0;
        uint32_t                   LogicOp                                                       = 0;
        uint32_t                   ColorBlendAttachmentCount                                     = 0;
        uint32_t                   UsesDynamicRendering                                          = 0;
        uint32_t                   RenderingViewMask                                             = 0;
        uint32_t                   RenderingColorAttachmentCount                                 = 0;
        uint32_t                   RenderingDepthAttachmentFormat                                = VK_FORMAT_UNDEFINED;
        uint32_t                   RenderingStencilAttachmentFormat                              = VK_FORMAT_UNDEFINED;
        uint32_t                   BlendConstantBits[4]                                          = {};
        uint32_t                   RenderingColorAttachmentFormats[kMaxPSOColorBlendAttachments] = {};
        PSOPipelineLayoutReference Layout                                                        = {};
        PSORenderPassReference     RenderPass                                                    = {};
        PSOGraphicsShaderStageKey  ShaderStages[kMaxPSOGraphicsShaderStages]                     = {};
        PSOVertexBindingKey        VertexBindings[kMaxPSOVertexBindings]                         = {};
        PSOVertexAttributeKey      VertexAttributes[kMaxPSOVertexAttributes]                     = {};
        PSOStencilOpStateKey       FrontStencil                                                  = {};
        PSOStencilOpStateKey       BackStencil                                                   = {};
        PSOColorBlendAttachmentKey ColorBlendAttachments[kMaxPSOColorBlendAttachments]           = {};

        bool                       operator==(const PSOGraphicsPipelineKey& other) const;
    };

    /// @brief Canonical representation of one descriptor-set layout binding.
    struct PSODescriptorSetLayoutBindingKey
    {
        uint64_t          ImmutableSamplerListIdentity = 0;
        uint32_t          Binding                      = 0;
        uint32_t          DescriptorCount              = 0;
        uint32_t          ShaderStages                 = 0;
        uint32_t          BindingFlags                 = 0;
        PSODescriptorKind DescriptorKind               = PSODescriptorKind::Sampler;
        uint8_t           Reserved[7]                  = {};

        bool              operator==(const PSODescriptorSetLayoutBindingKey& other) const;
    };
    static_assert(sizeof(PSODescriptorSetLayoutBindingKey) == sizeof(uint64_t) + sizeof(uint32_t) * 4 + sizeof(PSODescriptorKind) + 7);

    /// @brief Fixed-size descriptor-set layout identity with zeroed unused entries.
    struct PSODescriptorSetLayoutKey
    {
        uint32_t                         CreateFlags                     = 0;
        uint32_t                         BindingCount                    = 0;
        PSODescriptorSetLayoutBindingKey Bindings[kMaxPSOLayoutBindings] = {};

        bool                             operator==(const PSODescriptorSetLayoutKey& other) const;
    };

    /// @brief A cached layout ID or a complete external Vulkan-handle identity.
    struct PSOSetLayoutReference
    {
        uint64_t              CachedIdentity = 0;
        VkDescriptorSetLayout ExternalHandle = VK_NULL_HANDLE;

        bool                  operator==(const PSOSetLayoutReference& other) const;
    };

    /// @brief Canonical representation of one push-constant range.
    struct PSOPushConstantRangeKey
    {
        uint32_t Offset       = 0;
        uint32_t Size         = 0;
        uint32_t ShaderStages = 0;

        bool     operator==(const PSOPushConstantRangeKey& other) const;
    };

    /// @brief Fixed-size pipeline-layout identity with zeroed unused entries.
    struct PSOPipelineLayoutKey
    {
        uint32_t                SetLayoutCount                           = 0;
        uint32_t                PushConstantCount                        = 0;
        PSOSetLayoutReference   SetLayouts[kMaxPSOSetLayouts]            = {};
        PSOPushConstantRangeKey PushConstants[kMaxPSOPushConstantRanges] = {};

        bool                    operator==(const PSOPipelineLayoutKey& other) const;
    };

    /// @brief Persistable, resolvable graphics or compute pipeline warmup input.
    struct PSOWarmupRecipe
    {
        enum class Kind : uint32_t
        {
            Compute  = 1,
            Graphics = 2,
        };

        Kind                          RecipeKind                               = Kind::Compute;
        uint32_t                      Reserved                                 = 0;
        uint64_t                      ShaderContentHash                        = 0;
        char                          ShaderName[kMaxPSOWarmupShaderNameBytes] = {};
        PSOComputePipelineKey         Compute                                  = {};
        PSOGraphicsPipelineKey        Graphics                                 = {};
        PSOCompatibilityRenderPassKey Compatibility                            = {};
    };

    /// @brief Render-thread-only counters for cache lookup and persistent-cache activity.
    struct PSOCacheTelemetry
    {
        uint64_t SamplerHits                   = 0;
        uint64_t SamplerMisses                 = 0;
        uint64_t DescriptorSetLayoutHits       = 0;
        uint64_t DescriptorSetLayoutMisses     = 0;
        uint64_t PipelineLayoutHits            = 0;
        uint64_t PipelineLayoutMisses          = 0;
        uint64_t CompatibilityRenderPassHits   = 0;
        uint64_t CompatibilityRenderPassMisses = 0;
        uint64_t ComputePipelineHits           = 0;
        uint64_t ComputePipelineMisses         = 0;
        uint64_t GraphicsPipelineHits          = 0;
        uint64_t GraphicsPipelineMisses        = 0;
        uint64_t PersistentCacheLoadAttempts   = 0;
        uint64_t PersistentCacheLoadSuccesses  = 0;
        uint64_t PersistentCacheLoadBytes      = 0;
        uint64_t PersistentCacheSaveAttempts   = 0;
        uint64_t PersistentCacheSaveSuccesses  = 0;
        uint64_t PersistentCacheSaveBytes      = 0;
        uint64_t InvalidatedComputePipelines   = 0;
        uint64_t InvalidatedGraphicsPipelines  = 0;
        uint64_t EvictedComputePipelines       = 0;
        uint64_t EvictedGraphicsPipelines      = 0;
        uint64_t AsyncPipelineRequests         = 0;
        uint64_t AsyncPipelineJobs             = 0;
        uint64_t AsyncPipelineJobOverflows     = 0;
        uint64_t AsyncPipelineWaiterOverflows  = 0;
        uint64_t AsyncPipelinePublishes        = 0;
        uint64_t AsyncPipelineStaleCompletions = 0;
        uint64_t WarmupRecipeLoads             = 0;
        uint64_t WarmupRecipeLoadSuccesses     = 0;
        uint64_t WarmupRecipeSaves             = 0;
        uint64_t WarmupRecipeSaveSuccesses     = 0;
        uint64_t WarmupRecipeSkips             = 0;
    };

    /// @brief Device-owned synchronous cache for driver cache, descriptor layouts, and pipeline layouts.
    class PSOCache
    {
    public:
        void                          Initialize(Hardwares::VulkanDevice* device);
        void                          Shutdown();

        /// @brief Returns a shared descriptor-set layout for an equivalent canonical request.
        VkDescriptorSetLayout         GetOrCreateDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& create_info);

        /// @brief Returns a shared sampler for an equivalent canonical request.
        VkSampler                     GetOrCreateSampler(const VkSamplerCreateInfo& create_info);

        /// @brief Returns a shared pipeline layout for equivalent set-layout and push-constant state.
        VkPipelineLayout              GetOrCreatePipelineLayout(const VkPipelineLayoutCreateInfo& create_info);

        /// @brief Returns a compatibility render pass for graphics-pipeline creation only.
        VkRenderPass                  GetOrCreateCompatibilityRenderPass(const RenderPasses::Attachment& attachment);

        /// @brief Returns a shared compute pipeline for an equivalent shader generation and layout.
        VkPipeline                    GetOrCreateComputePipeline(const VkComputePipelineCreateInfo& create_info, uint32_t shader_generation);

        /// @brief Returns a shared graphics pipeline for equivalent fixed state and shader generations.
        VkPipeline                    GetOrCreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& create_info, uint32_t shader_generation);

        /// @brief Queues an immutable compute-pipeline job and returns a ready pipeline when one already exists.
        VkPipeline                    RequestComputePipelineAsync(const VkComputePipelineCreateInfo& create_info, uint32_t shader_generation, void* callback_context, PSOPipelineReadyFn callback, bool critical = false);

        /// @brief Queues an immutable graphics-pipeline job and returns a ready pipeline when one already exists.
        VkPipeline                    RequestGraphicsPipelineAsync(const VkGraphicsPipelineCreateInfo& create_info, uint32_t shader_generation, void* callback_context, PSOPipelineReadyFn callback, bool critical = false);

        /// @brief Publishes worker results, returns worker caches, and starts queued jobs on the render thread.
        void                          FlushAsyncPipelineJobs();

        /// @brief Cancels queued jobs and waits for active workers before device or shader teardown.
        void                          StopAsyncPipelineCompilation();

        /// @brief Defers shader-module destruction until no immutable asynchronous job references it.
        void                          RetireShaderModule(VkShaderModule shader_module);

        /// @brief Pins a pipeline borrowed by a live pass against age-based eviction.
        void                          PinPipeline(VkPipeline pipeline);

        /// @brief Releases a pass borrow so the pipeline can be evicted when it becomes old.
        void                          UnpinPipeline(VkPipeline pipeline);

        /// @brief Timeline-retires unpinned pipelines that have not been requested for the configured age.
        void                          EvictUnusedPipelines(uint64_t current_frame);

        /// @brief Loads a validated driver pipeline-cache blob from the configured writable VFS mount.
        bool                          LoadDriverPipelineCache(Core::VFS::IVFSContext& vfs);

        /// @brief Writes the current driver pipeline cache through a temporary VFS file and replacement rename.
        bool                          SaveDriverPipelineCache(Core::VFS::IVFSContext& vfs);

        /// @brief Retires every cached pipeline that references the replaced shader generation.
        void                          InvalidateShaderModules(const VkShaderModule* shader_modules, uint32_t module_count, uint32_t generation);

        /// @brief Loads and resolves valid pipeline warmup recipes from the configured VFS cache mount.
        bool                          LoadWarmupRecipes(Core::VFS::IVFSContext& vfs);

        /// @brief Persists the resolvable recipes observed during this run to the VFS cache mount.
        bool                          SaveWarmupRecipes(Core::VFS::IVFSContext& vfs);

        /// @brief Records a compute request for a future boot-time warmup.
        void                          RecordComputeWarmup(cstring shader_name, const PSOComputePipelineKey& key);

        /// @brief Records a graphics request and compatibility state for a future boot-time warmup.
        void                          RecordGraphicsWarmup(cstring shader_name, const PSOGraphicsPipelineKey& key, const PSOCompatibilityRenderPassKey& compatibility);

        /// @brief Returns render-thread-only cache telemetry.
        const PSOCacheTelemetry&      GetTelemetry() const;

        /// @brief Emits the cache telemetry summary through the rendering log channel.
        void                          LogTelemetry() const;

        /// @brief Returns the device-wide driver pipeline cache for synchronous pipeline creation.
        VkPipelineCache               GetDriverPipelineCache() const;

        /// @brief Builds a canonical descriptor-set layout key without creating a Vulkan object.
        PSODescriptorSetLayoutKey     MakeDescriptorSetLayoutKey(const VkDescriptorSetLayoutCreateInfo& create_info);

        /// @brief Builds a canonical sampler key without creating a Vulkan object.
        static PSOSamplerKey          MakeSamplerKey(const VkSamplerCreateInfo& create_info);

        /// @brief Builds a canonical pipeline-layout key without creating a Vulkan object.
        PSOPipelineLayoutKey          MakePipelineLayoutKey(const VkPipelineLayoutCreateInfo& create_info) const;

        /// @brief Builds a canonical compatibility render-pass key without creating a Vulkan object.
        PSOCompatibilityRenderPassKey MakeCompatibilityRenderPassKey(const RenderPasses::Attachment& attachment) const;

        /// @brief Builds a canonical compute-pipeline key without creating a Vulkan object.
        PSOComputePipelineKey         MakeComputePipelineKey(const VkComputePipelineCreateInfo& create_info, uint32_t shader_generation) const;

        /// @brief Builds a canonical graphics-pipeline key without creating a Vulkan object.
        PSOGraphicsPipelineKey        MakeGraphicsPipelineKey(const VkGraphicsPipelineCreateInfo& create_info, uint32_t shader_generation) const;

    private:
        struct DescriptorSetLayoutEntry
        {
            VkDescriptorSetLayout Handle   = VK_NULL_HANDLE;
            uint64_t              Identity = 0;
        };

        struct SamplerEntry
        {
            VkSampler Handle = VK_NULL_HANDLE;
        };

        struct ImmutableSamplerListEntry
        {
            uint64_t  Identity                           = 0;
            VkSampler Samplers[kMaxPSOImmutableSamplers] = {};
        };

        struct PipelineLayoutEntry
        {
            VkPipelineLayout Handle   = VK_NULL_HANDLE;
            uint64_t         Identity = 0;
        };

        struct CompatibilityRenderPassEntry
        {
            VkRenderPass Handle   = VK_NULL_HANDLE;
            uint64_t     Identity = 0;
        };

        struct ComputePipelineEntry
        {
            VkPipeline        Handle                       = VK_NULL_HANDLE;
            uint64_t          LastUsedFrame                = 0;
            uint32_t          PinCount                     = 0;
            uint32_t          Generation                   = 0;
            uint32_t          AsyncJobIndex                = UINT32_MAX;
            uint32_t          WaiterCount                  = 0;
            PSOPipelineState  State                        = PSOPipelineState::Discarded;
            PSOPipelineWaiter Waiters[kMaxPSOAsyncWaiters] = {};
        };

        struct GraphicsPipelineEntry
        {
            VkPipeline        Handle                       = VK_NULL_HANDLE;
            uint64_t          LastUsedFrame                = 0;
            uint32_t          PinCount                     = 0;
            uint32_t          Generation                   = 0;
            uint32_t          AsyncJobIndex                = UINT32_MAX;
            uint32_t          WaiterCount                  = 0;
            PSOPipelineState  State                        = PSOPipelineState::Discarded;
            PSOPipelineWaiter Waiters[kMaxPSOAsyncWaiters] = {};
        };

        enum class AsyncPipelineKind : uint8_t
        {
            Compute = 0,
            Graphics,
        };

        enum class AsyncPipelineJobState : uint8_t
        {
            Free = 0,
            Queued,
            Running,
            Cancelled,
        };

        /// @brief Immutable worker input. The render thread owns state changes; workers only read it.
        struct AsyncPipelineJob
        {
            PSOCache*              Cache            = nullptr;
            AsyncPipelineKind      Kind             = AsyncPipelineKind::Compute;
            AsyncPipelineJobState  State            = AsyncPipelineJobState::Free;
            uint32_t               Index            = UINT32_MAX;
            uint32_t               Generation       = 0;
            uint32_t               WorkerCacheIndex = UINT32_MAX;
            uint64_t               Hash             = 0;
            VkPipelineLayout       PipelineLayout   = VK_NULL_HANDLE;
            VkRenderPass           RenderPass       = VK_NULL_HANDLE;
            PSOComputePipelineKey  ComputeKey       = {};
            PSOGraphicsPipelineKey GraphicsKey      = {};
        };

        /// @brief Worker-to-render-thread completion record; the worker cache is no longer in use.
        struct AsyncPipelineCompletion
        {
            uint32_t   JobIndex         = UINT32_MAX;
            uint32_t   WorkerCacheIndex = UINT32_MAX;
            VkPipeline Pipeline         = VK_NULL_HANDLE;
            VkResult   Result           = VK_ERROR_UNKNOWN;
        };

        /// @brief A shader module awaiting asynchronous job completion before deferred destruction.
        struct RetiredShaderModule
        {
            VkShaderModule Handle = VK_NULL_HANDLE;
        };

        /// @brief A waiter result held until bucket mutation is complete.
        struct PendingPipelineNotification
        {
            PSOPipelineWaiter Waiter   = {};
            VkPipeline        Pipeline = VK_NULL_HANDLE;
        };

        struct ShaderInvalidationContext
        {
            PSOCache*             Cache         = nullptr;
            const VkShaderModule* ShaderModules = nullptr;
            uint32_t              ModuleCount   = 0;
            uint32_t              Generation    = 0;
        };

        struct DescriptorSetLayoutIdentityContext
        {
            VkDescriptorSetLayout Handle   = VK_NULL_HANDLE;
            uint64_t              Identity = 0;
        };

        struct ImmutableSamplerListIdentityContext
        {
            uint64_t                         Identity = 0;
            const ImmutableSamplerListEntry* Entry    = nullptr;
        };

        struct PipelineLayoutIdentityContext
        {
            VkPipelineLayout Handle   = VK_NULL_HANDLE;
            uint64_t         Identity = 0;
        };

        struct CompatibilityRenderPassIdentityContext
        {
            VkRenderPass Handle   = VK_NULL_HANDLE;
            uint64_t     Identity = 0;
        };

        struct PipelinePinContext
        {
            VkPipeline Handle        = VK_NULL_HANDLE;
            uint64_t   LastUsedFrame = 0;
            bool       Pin           = false;
        };

        struct PipelineEvictionContext
        {
            PSOCache* Cache        = nullptr;
            uint64_t  CurrentFrame = 0;
        };

        uint64_t                         HashSamplerKey(const PSOSamplerKey& key) const;
        uint64_t                         HashImmutableSamplerListKey(const PSOImmutableSamplerListKey& key) const;
        uint64_t                         HashDescriptorSetLayoutKey(const PSODescriptorSetLayoutKey& key) const;
        uint64_t                         HashPipelineLayoutKey(const PSOPipelineLayoutKey& key) const;
        uint64_t                         HashCompatibilityRenderPassKey(const PSOCompatibilityRenderPassKey& key) const;
        uint64_t                         HashComputePipelineKey(const PSOComputePipelineKey& key) const;
        uint64_t                         HashGraphicsPipelineKey(const PSOGraphicsPipelineKey& key) const;
        uint64_t                         GetSetLayoutIdentity(VkDescriptorSetLayout layout) const;
        uint64_t                         GetOrCreateImmutableSamplerList(const VkSampler* samplers, uint32_t sampler_count);
        const ImmutableSamplerListEntry* FindImmutableSamplerList(uint64_t identity) const;
        uint64_t                         GetPipelineLayoutIdentity(VkPipelineLayout layout) const;
        uint64_t                         GetCompatibilityRenderPassIdentity(VkRenderPass render_pass) const;
        uint64_t                         GetCurrentFrameMarker() const;
        void                             RetirePipeline(VkPipeline pipeline) const;
        void                             RetireShaderModuleNow(VkShaderModule shader_module) const;
        bool                             IsShaderModulePinnedByAsyncJob(VkShaderModule shader_module) const;
        void                             DrainRetiredShaderModules();
        bool                             TryCheckoutWorkerPipelineCache(uint32_t* worker_cache_index);
        void                             ReturnWorkerPipelineCache(uint32_t worker_cache_index);
        uint32_t                         AllocateAsyncPipelineJob();
        void                             CancelAsyncPipelineJob(uint32_t job_index);
        void                             DispatchQueuedAsyncPipelineJobs();
        void                             QueuePipelineNotification(const PSOPipelineWaiter& waiter, VkPipeline pipeline);
        void                             QueuePipelineWaiters(ComputePipelineEntry& entry, VkPipeline pipeline);
        void                             QueuePipelineWaiters(GraphicsPipelineEntry& entry, VkPipeline pipeline);
        void                             DispatchPipelineNotifications();
        bool                             AppendWaiter(ComputePipelineEntry& entry, const PSOPipelineWaiter& waiter);
        bool                             AppendWaiter(GraphicsPipelineEntry& entry, const PSOPipelineWaiter& waiter);
        void                             PublishAsyncCompletion(const AsyncPipelineCompletion& completion);
        static void                      CompileAsyncPipelineJob(void* context);
        static VkResult                  CompileAsyncComputePipeline(const AsyncPipelineJob& job, VkDevice device, VkPipelineCache worker_cache, VkPipeline* pipeline);
        static VkResult                  CompileAsyncGraphicsPipeline(const AsyncPipelineJob& job, VkDevice device, VkPipelineCache worker_cache, VkPipeline* pipeline);
        VkRenderPass                     GetOrCreateCompatibilityRenderPass(const PSOCompatibilityRenderPassKey& key);
        uint64_t                         ComputeShaderContentHash(Core::VFS::IVFSContext& vfs, cstring shader_name, VkShaderStageFlags stages) const;
        bool                             WarmupComputePipeline(const PSOWarmupRecipe& recipe);
        bool                             WarmupGraphicsPipeline(const PSOWarmupRecipe& recipe);
        void                             RecordWarmupRecipe(const PSOWarmupRecipe& recipe);
        static void                      DestroyGraphicsPipeline(void* context, GraphicsPipelineEntry& entry);
        static void                      DestroyComputePipeline(void* context, ComputePipelineEntry& entry);
        static void                      DestroyCompatibilityRenderPass(void* context, CompatibilityRenderPassEntry& entry);
        static void                      DestroyPipelineLayout(void* context, PipelineLayoutEntry& entry);
        static void                      DestroyDescriptorSetLayout(void* context, DescriptorSetLayoutEntry& entry);
        static void                      DestroySampler(void* context, SamplerEntry& entry);
        static bool                      RemoveInvalidatedComputePipeline(void* context, const PSOComputePipelineKey& key, ComputePipelineEntry& entry);
        static bool                      RemoveInvalidatedGraphicsPipeline(void* context, const PSOGraphicsPipelineKey& key, GraphicsPipelineEntry& entry);
        static void                      FindDescriptorSetLayoutIdentity(void* context, const DescriptorSetLayoutEntry& entry);
        static void                      FindImmutableSamplerListIdentity(void* context, const ImmutableSamplerListEntry& entry);
        static void                      FindPipelineLayoutIdentity(void* context, const PipelineLayoutEntry& entry);
        static void                      FindCompatibilityRenderPassIdentity(void* context, const CompatibilityRenderPassEntry& entry);
        static void                      AdjustComputePipelinePin(void* context, ComputePipelineEntry& entry);
        static void                      AdjustGraphicsPipelinePin(void* context, GraphicsPipelineEntry& entry);
        static bool                      RemoveEvictedComputePipeline(void* context, const PSOComputePipelineKey& key, ComputePipelineEntry& entry);
        static bool                      RemoveEvictedGraphicsPipeline(void* context, const PSOGraphicsPipelineKey& key, GraphicsPipelineEntry& entry);

    private:
        Hardwares::VulkanDevice*                                                                                                                                              m_device                                                        = nullptr;
        VkPipelineCache                                                                                                                                                       m_driver_pipeline_cache                                         = VK_NULL_HANDLE;
        uint64_t                                                                                                                                                              m_next_layout_identity                                          = 1;
        uint64_t                                                                                                                                                              m_next_immutable_sampler_list_identity                          = 1;
        uint64_t                                                                                                                                                              m_next_pipeline_layout_identity                                 = 1;
        uint64_t                                                                                                                                                              m_next_compatibility_render_pass_identity                       = 1;
        PSOCacheTelemetry                                                                                                                                                     m_telemetry                                                     = {};
        Core::VFS::IVFSContext*                                                                                                                                               m_warmup_vfs                                                    = nullptr;
        PSOWarmupRecipe                                                                                                                                                       m_warmup_recipes[kMaxPSOWarmupRecipes]                          = {};
        uint32_t                                                                                                                                                              m_warmup_recipe_count                                           = 0;
        bool                                                                                                                                                                  m_accept_async_jobs                                             = true;
        uint32_t                                                                                                                                                              m_worker_pipeline_cache_count                                   = 0;
        PaddedAtomic<uint32_t>                                                                                                                                                m_available_worker_pipeline_caches                              = {};
        PaddedAtomic<uint32_t>                                                                                                                                                m_active_async_jobs                                             = {};
        VkPipelineCache                                                                                                                                                       m_worker_pipeline_caches[kMaxPSOWorkerPipelineCaches]           = {};
        AsyncPipelineJob                                                                                                                                                      m_async_jobs[kMaxPSOAsyncJobs]                                  = {};
        Core::Containers::MPSCQueue<AsyncPipelineCompletion, kMaxPSOAsyncJobs>                                                                                                m_async_completions                                             = {};
        RetiredShaderModule                                                                                                                                                   m_retired_shader_modules[kMaxPSORetiredShaderModules]           = {};
        PendingPipelineNotification                                                                                                                                           m_pending_notifications[kMaxPSOAsyncJobs * kMaxPSOAsyncWaiters] = {};
        uint32_t                                                                                                                                                              m_retired_shader_module_count                                   = 0;
        uint32_t                                                                                                                                                              m_pending_notification_count                                    = 0;
        PSOCollisionBuckets<PSOSamplerKey, SamplerEntry, kPSOSamplerBucketCount, kPSOSamplerEntriesPerBucket>                                                                 m_samplers                                                      = {};
        PSOCollisionBuckets<PSOImmutableSamplerListKey, ImmutableSamplerListEntry, kPSOImmutableSamplerListBucketCount, kPSOImmutableSamplerListEntriesPerBucket>             m_immutable_sampler_lists                                       = {};
        PSOCollisionBuckets<PSODescriptorSetLayoutKey, DescriptorSetLayoutEntry, kPSOLayoutBucketCount, kPSOLayoutEntriesPerBucket>                                           m_descriptor_set_layouts                                        = {};
        PSOCollisionBuckets<PSOPipelineLayoutKey, PipelineLayoutEntry, kPSOPipelineLayoutBucketCount, kPSOPipelineLayoutEntriesPerBucket>                                     m_pipeline_layouts                                              = {};
        PSOCollisionBuckets<PSOCompatibilityRenderPassKey, CompatibilityRenderPassEntry, kPSOCompatibilityRenderPassBucketCount, kPSOCompatibilityRenderPassEntriesPerBucket> m_compatibility_render_passes                                   = {};
        PSOCollisionBuckets<PSOComputePipelineKey, ComputePipelineEntry, kPSOComputePipelineBucketCount, kPSOComputePipelineEntriesPerBucket>                                 m_compute_pipelines                                             = {};
        PSOCollisionBuckets<PSOGraphicsPipelineKey, GraphicsPipelineEntry, kPSOGraphicsPipelineBucketCount, kPSOGraphicsPipelineEntriesPerBucket>                             m_graphics_pipelines                                            = {};
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines
