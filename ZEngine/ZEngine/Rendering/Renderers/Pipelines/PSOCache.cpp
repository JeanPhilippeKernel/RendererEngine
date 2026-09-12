#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Rendering/Renderers/Base/Attachment.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <ZEngine/Rendering/Specifications/FormatSpecification.h>
#include <bit>
#include <type_traits>

namespace ZEngine::Rendering::Renderers::Pipelines
{
    namespace
    {
        constexpr uint32_t           kSupportedDescriptorBindingFlags   = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        constexpr uint32_t           kSupportedDescriptorSetLayoutFlags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        constexpr VkShaderStageFlags kSupportedShaderStages             = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
        constexpr uint32_t           kPersistentCacheMagic              = 0x43504F5A; // ZOPC
        constexpr uint32_t           kPersistentCacheVersion            = 1;
        constexpr uint32_t           kPersistentCacheHeaderBytes        = 64;
        constexpr uint64_t           kMaxPersistentCacheBytes           = 32ULL * 1024ULL * 1024ULL;
        constexpr cstring            kPersistentCacheDirectory          = "/ZodiacEngine/cache";
        constexpr cstring            kPersistentCacheFile               = "/ZodiacEngine/cache/vulkan-pipeline-cache.bin";
        constexpr cstring            kPersistentCacheTempFile           = "/ZodiacEngine/cache/vulkan-pipeline-cache.tmp";
        constexpr uint32_t           kPersistentCacheEngineVersion      = ZENGINE_PIPELINE_CACHE_ENGINE_VERSION;
        constexpr uint32_t           kWarmupRecipeMagic                 = 0x5257505A; // ZPWR
        constexpr uint32_t           kWarmupRecipeVersion               = 3;
        constexpr uint32_t           kWarmupRecipeHeaderBytes           = 64;
        constexpr cstring            kWarmupRecipeFile                  = "/ZodiacEngine/cache/vulkan-pipeline-warmup.bin";
        constexpr cstring            kWarmupRecipeTempFile              = "/ZodiacEngine/cache/vulkan-pipeline-warmup.tmp";

        static_assert(std::is_trivially_copyable_v<PSOWarmupRecipe>);

#if defined(_WIN32)
        constexpr uint32_t kPersistentCachePlatform = 1;
#elif defined(__APPLE__)
        constexpr uint32_t kPersistentCachePlatform = 2;
#elif defined(__linux__)
        constexpr uint32_t kPersistentCachePlatform = 3;
#else
        constexpr uint32_t kPersistentCachePlatform = 0;
#endif

        enum PersistentCacheHeaderOffset : uint32_t
        {
            PersistentCacheMagicOffset         = 0,
            PersistentCacheVersionOffset       = 4,
            PersistentCacheHeaderSizeOffset    = 8,
            PersistentCacheVendorIdOffset      = 12,
            PersistentCacheDeviceIdOffset      = 16,
            PersistentCacheDriverVersionOffset = 20,
            PersistentCacheApiVersionOffset    = 24,
            PersistentCacheEngineVersionOffset = 28,
            PersistentCachePlatformOffset      = 32,
            PersistentCacheUuidOffset          = 36,
            PersistentCacheBlobSizeOffset      = 52,
            PersistentCacheCrc32Offset         = 60,
        };

        enum WarmupRecipeHeaderOffset : uint32_t
        {
            WarmupRecipeMagicOffset         = 0,
            WarmupRecipeVersionOffset       = 4,
            WarmupRecipeHeaderSizeOffset    = 8,
            WarmupRecipeRecordSizeOffset    = 12,
            WarmupRecipeCountOffset         = 16,
            WarmupRecipeEngineVersionOffset = 20,
            WarmupRecipePlatformOffset      = 24,
            WarmupRecipePointerSizeOffset   = 28,
            WarmupRecipePayloadBytesOffset  = 32,
            WarmupRecipeCrc32Offset         = 40,
        };

        uint64_t HashAppend(uint64_t hash, uint64_t value)
        {
            hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
            return hash;
        }

        uint64_t HashSpecializationKey(uint64_t hash, const PSOSpecializationKey& key)
        {
            hash = HashAppend(hash, key.EntryCount);
            for (uint32_t entry_index = 0; entry_index < key.EntryCount; ++entry_index)
            {
                const PSOSpecializationValueKey& entry = key.Entries[entry_index];
                hash                                   = HashAppend(hash, entry.ConstantId);
                hash                                   = HashAppend(hash, entry.ByteCount);
                for (uint32_t byte_index = 0; byte_index < entry.ByteCount; ++byte_index)
                    hash = HashAppend(hash, entry.Data[byte_index]);
            }
            return hash;
        }

        bool IsSpecializationKeyValid(const PSOSpecializationKey& key)
        {
            if (key.EntryCount > kMaxPSOSpecializationEntries)
                return false;

            for (uint32_t entry_index = 0; entry_index < key.EntryCount; ++entry_index)
            {
                const PSOSpecializationValueKey& entry = key.Entries[entry_index];
                if (entry.ByteCount == 0 || entry.ByteCount > kMaxPSOSpecializationValueBytes)
                    return false;
                if (entry_index > 0 && key.Entries[entry_index - 1].ConstantId >= entry.ConstantId)
                    return false;
            }
            return true;
        }

        PSOSpecializationKey MakeSpecializationKey(const VkSpecializationInfo* specialization_info)
        {
            PSOSpecializationKey key = {};
            if (!specialization_info || specialization_info->mapEntryCount == 0)
                return key;

            ZENGINE_VALIDATE_ASSERT(specialization_info->mapEntryCount <= kMaxPSOSpecializationEntries, "PSO specialization-constant entry capacity exceeded")
            ZENGINE_VALIDATE_ASSERT(specialization_info->pMapEntries != nullptr, "PSO specialization map entries are null")
            ZENGINE_VALIDATE_ASSERT(specialization_info->pData != nullptr && specialization_info->dataSize != 0, "PSO specialization data is null or empty")

            key.EntryCount          = specialization_info->mapEntryCount;
            const auto* source_data = static_cast<const uint8_t*>(specialization_info->pData);
            for (uint32_t entry_index = 0; entry_index < key.EntryCount; ++entry_index)
            {
                const VkSpecializationMapEntry& source      = specialization_info->pMapEntries[entry_index];
                PSOSpecializationValueKey&      destination = key.Entries[entry_index];
                ZENGINE_VALIDATE_ASSERT(source.size != 0 && source.size <= kMaxPSOSpecializationValueBytes, "PSO specialization-constant value capacity exceeded")
                ZENGINE_VALIDATE_ASSERT(source.offset <= specialization_info->dataSize && source.size <= specialization_info->dataSize - source.offset, "PSO specialization-constant range is outside its data block")
                destination.ConstantId = source.constantID;
                destination.ByteCount  = static_cast<uint32_t>(source.size);
                Helpers::secure_memcpy(destination.Data, sizeof(destination.Data), source_data + source.offset, source.size);
            }

            for (uint32_t entry_index = 1; entry_index < key.EntryCount; ++entry_index)
            {
                PSOSpecializationValueKey current = key.Entries[entry_index];
                uint32_t                  index   = entry_index;
                while (index > 0 && key.Entries[index - 1].ConstantId > current.ConstantId)
                {
                    key.Entries[index] = key.Entries[index - 1];
                    --index;
                }
                key.Entries[index] = current;
            }
            for (uint32_t entry_index = 1; entry_index < key.EntryCount; ++entry_index)
                ZENGINE_VALIDATE_ASSERT(key.Entries[entry_index - 1].ConstantId != key.Entries[entry_index].ConstantId, "PSO specialization constant IDs must be unique")
            return key;
        }

        void BuildSpecializationInfo(const PSOSpecializationKey& key, VkSpecializationMapEntry (&map_entries)[kMaxPSOSpecializationEntries], uint8_t (&data)[kMaxPSOSpecializationEntries * kMaxPSOSpecializationValueBytes], VkSpecializationInfo* out_info)
        {
            ZENGINE_VALIDATE_ASSERT(out_info != nullptr && IsSpecializationKeyValid(key), "PSO specialization key is invalid")
            *out_info = {};
            if (key.EntryCount == 0)
                return;

            size_t data_offset = 0;
            for (uint32_t entry_index = 0; entry_index < key.EntryCount; ++entry_index)
            {
                const PSOSpecializationValueKey& source = key.Entries[entry_index];
                map_entries[entry_index]                = {
                    .constantID = source.ConstantId,
                    .offset     = static_cast<uint32_t>(data_offset),
                    .size       = source.ByteCount,
                };
                Helpers::secure_memcpy(data + data_offset, kMaxPSOSpecializationEntries * kMaxPSOSpecializationValueBytes - data_offset, source.Data, source.ByteCount);
                data_offset += source.ByteCount;
            }
            out_info->mapEntryCount = key.EntryCount;
            out_info->pMapEntries   = map_entries;
            out_info->dataSize      = data_offset;
            out_info->pData         = data;
        }

        uint32_t CRC32(const uint8_t* data, size_t size)
        {
            uint32_t crc = 0xFFFFFFFFU;
            for (size_t i = 0; i < size; ++i)
            {
                crc ^= data[i];
                for (uint32_t bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
            }
            return ~crc;
        }

        void WriteUint32LE(uint8_t* destination, uint32_t value)
        {
            destination[0] = static_cast<uint8_t>(value);
            destination[1] = static_cast<uint8_t>(value >> 8U);
            destination[2] = static_cast<uint8_t>(value >> 16U);
            destination[3] = static_cast<uint8_t>(value >> 24U);
        }

        uint32_t ReadUint32LE(const uint8_t* source)
        {
            return static_cast<uint32_t>(source[0]) | (static_cast<uint32_t>(source[1]) << 8U) | (static_cast<uint32_t>(source[2]) << 16U) | (static_cast<uint32_t>(source[3]) << 24U);
        }

        void WriteUint64LE(uint8_t* destination, uint64_t value)
        {
            WriteUint32LE(destination, static_cast<uint32_t>(value));
            WriteUint32LE(destination + sizeof(uint32_t), static_cast<uint32_t>(value >> 32U));
        }

        uint64_t ReadUint64LE(const uint8_t* source)
        {
            return static_cast<uint64_t>(ReadUint32LE(source)) | (static_cast<uint64_t>(ReadUint32LE(source + sizeof(uint32_t))) << 32U);
        }

        void MakePersistentCacheHeader(uint8_t (&header)[kPersistentCacheHeaderBytes], const VkPhysicalDeviceProperties& properties, uint64_t blob_size, uint32_t crc32)
        {
            Helpers::secure_memset(header, 0, sizeof(header), sizeof(header));
            WriteUint32LE(header + PersistentCacheMagicOffset, kPersistentCacheMagic);
            WriteUint32LE(header + PersistentCacheVersionOffset, kPersistentCacheVersion);
            WriteUint32LE(header + PersistentCacheHeaderSizeOffset, kPersistentCacheHeaderBytes);
            WriteUint32LE(header + PersistentCacheVendorIdOffset, properties.vendorID);
            WriteUint32LE(header + PersistentCacheDeviceIdOffset, properties.deviceID);
            WriteUint32LE(header + PersistentCacheDriverVersionOffset, properties.driverVersion);
            WriteUint32LE(header + PersistentCacheApiVersionOffset, properties.apiVersion);
            WriteUint32LE(header + PersistentCacheEngineVersionOffset, kPersistentCacheEngineVersion);
            WriteUint32LE(header + PersistentCachePlatformOffset, kPersistentCachePlatform);
            Helpers::secure_memcpy(header + PersistentCacheUuidOffset, VK_UUID_SIZE, properties.pipelineCacheUUID, VK_UUID_SIZE);
            WriteUint64LE(header + PersistentCacheBlobSizeOffset, blob_size);
            WriteUint32LE(header + PersistentCacheCrc32Offset, crc32);
        }

        bool IsPersistentCacheHeaderValid(const uint8_t (&header)[kPersistentCacheHeaderBytes], const VkPhysicalDeviceProperties& properties, uint64_t file_size)
        {
            if (ReadUint32LE(header + PersistentCacheMagicOffset) != kPersistentCacheMagic || ReadUint32LE(header + PersistentCacheVersionOffset) != kPersistentCacheVersion || ReadUint32LE(header + PersistentCacheHeaderSizeOffset) != kPersistentCacheHeaderBytes)
                return false;
            if (ReadUint32LE(header + PersistentCacheVendorIdOffset) != properties.vendorID || ReadUint32LE(header + PersistentCacheDeviceIdOffset) != properties.deviceID || ReadUint32LE(header + PersistentCacheDriverVersionOffset) != properties.driverVersion || ReadUint32LE(header + PersistentCacheApiVersionOffset) != properties.apiVersion || ReadUint32LE(header + PersistentCacheEngineVersionOffset) != kPersistentCacheEngineVersion || ReadUint32LE(header + PersistentCachePlatformOffset) != kPersistentCachePlatform)
                return false;
            if (Helpers::secure_memcmp(header + PersistentCacheUuidOffset, VK_UUID_SIZE, properties.pipelineCacheUUID, VK_UUID_SIZE, VK_UUID_SIZE) != 0)
                return false;
            const uint64_t blob_size = ReadUint64LE(header + PersistentCacheBlobSizeOffset);
            return blob_size <= kMaxPersistentCacheBytes && file_size == kPersistentCacheHeaderBytes + blob_size;
        }

        void MakeWarmupRecipeHeader(uint8_t (&header)[kWarmupRecipeHeaderBytes], uint32_t recipe_count, uint64_t payload_size, uint32_t crc32)
        {
            Helpers::secure_memset(header, 0, sizeof(header), sizeof(header));
            WriteUint32LE(header + WarmupRecipeMagicOffset, kWarmupRecipeMagic);
            WriteUint32LE(header + WarmupRecipeVersionOffset, kWarmupRecipeVersion);
            WriteUint32LE(header + WarmupRecipeHeaderSizeOffset, kWarmupRecipeHeaderBytes);
            WriteUint32LE(header + WarmupRecipeRecordSizeOffset, sizeof(PSOWarmupRecipe));
            WriteUint32LE(header + WarmupRecipeCountOffset, recipe_count);
            WriteUint32LE(header + WarmupRecipeEngineVersionOffset, kPersistentCacheEngineVersion);
            WriteUint32LE(header + WarmupRecipePlatformOffset, kPersistentCachePlatform);
            WriteUint32LE(header + WarmupRecipePointerSizeOffset, sizeof(void*));
            WriteUint64LE(header + WarmupRecipePayloadBytesOffset, payload_size);
            WriteUint32LE(header + WarmupRecipeCrc32Offset, crc32);
        }

        bool IsWarmupRecipeHeaderValid(const uint8_t (&header)[kWarmupRecipeHeaderBytes], uint64_t file_size)
        {
            if (ReadUint32LE(header + WarmupRecipeMagicOffset) != kWarmupRecipeMagic || ReadUint32LE(header + WarmupRecipeVersionOffset) != kWarmupRecipeVersion || ReadUint32LE(header + WarmupRecipeHeaderSizeOffset) != kWarmupRecipeHeaderBytes || ReadUint32LE(header + WarmupRecipeRecordSizeOffset) != sizeof(PSOWarmupRecipe))
                return false;
            if (ReadUint32LE(header + WarmupRecipeEngineVersionOffset) != kPersistentCacheEngineVersion || ReadUint32LE(header + WarmupRecipePlatformOffset) != kPersistentCachePlatform || ReadUint32LE(header + WarmupRecipePointerSizeOffset) != sizeof(void*))
                return false;
            const uint32_t count        = ReadUint32LE(header + WarmupRecipeCountOffset);
            const uint64_t payload_size = ReadUint64LE(header + WarmupRecipePayloadBytesOffset);
            return count <= kMaxPSOWarmupRecipes && payload_size == static_cast<uint64_t>(count) * sizeof(PSOWarmupRecipe) && file_size == kWarmupRecipeHeaderBytes + payload_size;
        }

        bool CopyWarmupShaderName(char (&destination)[kMaxPSOWarmupShaderNameBytes], cstring source)
        {
            if (!source)
                return false;
            const size_t source_size = Helpers::secure_strlen(source);
            if (source_size == 0 || source_size >= sizeof(destination))
                return false;
            Helpers::secure_memset(destination, 0, sizeof(destination), sizeof(destination));
            Helpers::secure_memcpy(destination, sizeof(destination), source, source_size);
            return true;
        }

        bool ReadFileExactly(Core::VFS::IVFSFile& file, uint8_t* destination, size_t size, uint64_t offset)
        {
            auto read = file.Read(Core::Containers::ArrayView<uint8_t>(destination, size), offset);
            return read.Succeeded() && read.Value() == size;
        }

        bool WriteFileExactly(Core::VFS::IVFSFile& file, const uint8_t* source, size_t size, uint64_t offset)
        {
            auto written = file.Write(Core::Containers::ArrayView<const uint8_t>(source, size), offset);
            return written.Succeeded() && written.Value() == size;
        }

        size_t GetWarmupShaderNameLength(const char (&shader_name)[kMaxPSOWarmupShaderNameBytes])
        {
            for (size_t index = 0; index < sizeof(shader_name); ++index)
            {
                if (shader_name[index] == '\0')
                    return index;
            }
            return sizeof(shader_name);
        }

        bool IsVulkanBool(VkBool32 value)
        {
            return value == VK_FALSE || value == VK_TRUE;
        }

        bool IsWarmupRecipeValid(const PSOWarmupRecipe& recipe)
        {
            if (recipe.ShaderContentHash == 0 || GetWarmupShaderNameLength(recipe.ShaderName) == 0 || GetWarmupShaderNameLength(recipe.ShaderName) == sizeof(recipe.ShaderName))
                return false;

            if (recipe.RecipeKind == PSOWarmupRecipe::Kind::Compute)
            {
                return recipe.Compute.CreateFlags == 0 && recipe.Compute.Shader.ModuleIdentity == 0 && recipe.Compute.Shader.Generation == 0 && recipe.Compute.Shader.Stage == PSOShaderStage::Compute && IsSpecializationKeyValid(recipe.Compute.Shader.Specialization) && recipe.Compute.Layout.CachedIdentity == 0 && recipe.Compute.Layout.ExternalHandle == VK_NULL_HANDLE;
            }

            if (recipe.RecipeKind != PSOWarmupRecipe::Kind::Graphics)
                return false;

            const PSOGraphicsPipelineKey&        graphics      = recipe.Graphics;
            const PSOCompatibilityRenderPassKey& compatibility = recipe.Compatibility;
            if (graphics.CreateFlags != 0 || graphics.Subpass != 0 || graphics.ShaderStageCount == 0 || graphics.ShaderStageCount > kMaxPSOGraphicsShaderStages || graphics.VertexBindingCount > kMaxPSOVertexBindings || graphics.VertexAttributeCount > kMaxPSOVertexAttributes || graphics.ColorBlendAttachmentCount > kMaxPSOColorBlendAttachments || graphics.RenderingColorAttachmentCount > kMaxPSOColorBlendAttachments || graphics.ViewportCount != 1 || graphics.ScissorCount != 1 || graphics.Layout.CachedIdentity != 0 || graphics.Layout.ExternalHandle != VK_NULL_HANDLE ||
                graphics.RenderPass.CachedIdentity != 0 || graphics.RenderPass.ExternalHandle != VK_NULL_HANDLE)
                return false;
            if (!IsVulkanBool(graphics.UsesDynamicRendering))
                return false;

            if (graphics.UsesDynamicRendering == VK_TRUE)
            {
                if (graphics.RenderingColorAttachmentCount != graphics.ColorBlendAttachmentCount || compatibility.AttachmentCount != 0 || compatibility.ColorAttachmentCount != 0 || compatibility.DepthAttachmentIndex != VK_ATTACHMENT_UNUSED)
                    return false;
                for (uint32_t index = 0; index < graphics.RenderingColorAttachmentCount; ++index)
                {
                    if (graphics.RenderingColorAttachmentFormats[index] == VK_FORMAT_UNDEFINED)
                        return false;
                }
            }
            else
            {
                if (compatibility.AttachmentCount == 0 || compatibility.AttachmentCount > kMaxPSOCompatibilityAttachments || compatibility.ColorAttachmentCount > compatibility.AttachmentCount || graphics.ColorBlendAttachmentCount != compatibility.ColorAttachmentCount)
                    return false;
                if (compatibility.DepthAttachmentIndex != VK_ATTACHMENT_UNUSED && compatibility.DepthAttachmentIndex >= compatibility.AttachmentCount)
                    return false;
                for (uint32_t index = 0; index < compatibility.AttachmentCount; ++index)
                {
                    const PSOCompatibilityAttachmentKey& attachment = compatibility.Attachments[index];
                    if (attachment.Format == VK_FORMAT_UNDEFINED || attachment.Samples != graphics.RasterizationSamples)
                        return false;
                }
            }

            VkShaderStageFlags stage_mask = 0;
            for (uint32_t index = 0; index < graphics.ShaderStageCount; ++index)
            {
                const PSOGraphicsShaderStageKey& stage = graphics.ShaderStages[index];
                if (stage.ModuleIdentity != 0 || stage.Generation != 0 || !IsSpecializationKeyValid(stage.Specialization) || (stage.Stage != VK_SHADER_STAGE_VERTEX_BIT && stage.Stage != VK_SHADER_STAGE_FRAGMENT_BIT) || (stage_mask & stage.Stage) != 0)
                    return false;
                stage_mask |= stage.Stage;
            }

            if (!IsVulkanBool(graphics.PrimitiveRestartEnable) || !IsVulkanBool(graphics.DepthClampEnable) || !IsVulkanBool(graphics.RasterizerDiscardEnable) || !IsVulkanBool(graphics.DepthBiasEnable) || !IsVulkanBool(graphics.SampleShadingEnable) || !IsVulkanBool(graphics.AlphaToCoverageEnable) || !IsVulkanBool(graphics.AlphaToOneEnable) || !IsVulkanBool(graphics.HasDepthStencilState) || !IsVulkanBool(graphics.DepthTestEnable) || !IsVulkanBool(graphics.DepthWriteEnable) || !IsVulkanBool(graphics.DepthBoundsTestEnable) ||
                !IsVulkanBool(graphics.StencilTestEnable) || !IsVulkanBool(graphics.LogicOpEnable))
                return false;

            return graphics.RasterizationSamples != 0;
        }

        Shaders::Shader* CompileWarmupShader(Hardwares::VulkanDevice* device, const char (&shader_name)[kMaxPSOWarmupShaderNameBytes])
        {
            const size_t shader_name_length = GetWarmupShaderNameLength(shader_name);
            if (shader_name_length == 0 || shader_name_length == sizeof(shader_name))
                return nullptr;

            char* persistent_name = ZPushString(device->Arena, shader_name_length + 1);
            Helpers::secure_memcpy(persistent_name, shader_name_length + 1, shader_name, shader_name_length);
            persistent_name[shader_name_length]               = '\0';

            Specifications::ShaderSpecification specification = {};
            specification.Name                                = persistent_name;
            const Helpers::Handle<Shaders::Shader> handle     = device->CompileShader(specification);
            return handle ? device->ShaderManager.Access(handle) : nullptr;
        }

        const VkPipelineShaderStageCreateInfo* FindShaderStage(const Shaders::Shader& shader, VkShaderStageFlagBits stage)
        {
            for (const VkPipelineShaderStageCreateInfo& create_info : shader.ShaderCreateInfos)
            {
                if (create_info.stage == stage)
                    return &create_info;
            }
            return nullptr;
        }

        VkStencilOpState MakeVulkanStencilState(const PSOStencilOpStateKey& key)
        {
            return {
                .failOp      = static_cast<VkStencilOp>(key.FailOp),
                .passOp      = static_cast<VkStencilOp>(key.PassOp),
                .depthFailOp = static_cast<VkStencilOp>(key.DepthFailOp),
                .compareOp   = static_cast<VkCompareOp>(key.CompareOp),
                .compareMask = key.CompareMask,
                .writeMask   = key.WriteMask,
                .reference   = key.Reference,
            };
        }

        PSODescriptorKind ToPSODescriptorKind(VkDescriptorType descriptor_type)
        {
            switch (descriptor_type)
            {
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                    return PSODescriptorKind::Sampler;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    return PSODescriptorKind::CombinedImageSampler;
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    return PSODescriptorKind::SampledImage;
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    return PSODescriptorKind::StorageImage;
                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    return PSODescriptorKind::UniformTexelBuffer;
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    return PSODescriptorKind::StorageTexelBuffer;
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    return PSODescriptorKind::UniformBuffer;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    return PSODescriptorKind::StorageBuffer;
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    return PSODescriptorKind::UniformBufferDynamic;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    return PSODescriptorKind::StorageBufferDynamic;
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    return PSODescriptorKind::InputAttachment;
                default:
                    ZENGINE_VALIDATE_ASSERT(false, "Unsupported Vulkan descriptor type in PSO cache key")
                    return PSODescriptorKind::Sampler;
            }
        }

        VkDescriptorType ToVulkanDescriptorType(PSODescriptorKind descriptor_kind)
        {
            switch (descriptor_kind)
            {
                case PSODescriptorKind::Sampler:
                    return VK_DESCRIPTOR_TYPE_SAMPLER;
                case PSODescriptorKind::CombinedImageSampler:
                    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                case PSODescriptorKind::SampledImage:
                    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                case PSODescriptorKind::StorageImage:
                    return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                case PSODescriptorKind::UniformTexelBuffer:
                    return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                case PSODescriptorKind::StorageTexelBuffer:
                    return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                case PSODescriptorKind::UniformBuffer:
                    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                case PSODescriptorKind::StorageBuffer:
                    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                case PSODescriptorKind::UniformBufferDynamic:
                    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                case PSODescriptorKind::StorageBufferDynamic:
                    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                case PSODescriptorKind::InputAttachment:
                    return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            }
            ZENGINE_VALIDATE_ASSERT(false, "Unsupported PSO descriptor kind")
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        }

        PSOFilter ToPSOFilter(VkFilter filter)
        {
            switch (filter)
            {
                case VK_FILTER_NEAREST:
                    return PSOFilter::Nearest;
                case VK_FILTER_LINEAR:
                    return PSOFilter::Linear;
                default:
                    ZENGINE_VALIDATE_ASSERT(false, "Unsupported Vulkan sampler filter in PSO cache key")
                    return PSOFilter::Nearest;
            }
        }

        PSOSamplerMipmapMode ToPSOMipmapMode(VkSamplerMipmapMode mode)
        {
            switch (mode)
            {
                case VK_SAMPLER_MIPMAP_MODE_NEAREST:
                    return PSOSamplerMipmapMode::Nearest;
                case VK_SAMPLER_MIPMAP_MODE_LINEAR:
                    return PSOSamplerMipmapMode::Linear;
                default:
                    ZENGINE_VALIDATE_ASSERT(false, "Unsupported Vulkan sampler mipmap mode in PSO cache key")
                    return PSOSamplerMipmapMode::Nearest;
            }
        }

        PSOSamplerAddressMode ToPSOAddressMode(VkSamplerAddressMode mode)
        {
            switch (mode)
            {
                case VK_SAMPLER_ADDRESS_MODE_REPEAT:
                    return PSOSamplerAddressMode::Repeat;
                case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
                    return PSOSamplerAddressMode::MirroredRepeat;
                case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
                    return PSOSamplerAddressMode::ClampToEdge;
                case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
                    return PSOSamplerAddressMode::ClampToBorder;
                case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
                    return PSOSamplerAddressMode::MirrorClampToEdge;
                default:
                    ZENGINE_VALIDATE_ASSERT(false, "Unsupported Vulkan sampler address mode in PSO cache key")
                    return PSOSamplerAddressMode::Repeat;
            }
        }

        PSOCompareOp ToPSOCompareOp(VkCompareOp compare_op)
        {
            switch (compare_op)
            {
                case VK_COMPARE_OP_NEVER:
                    return PSOCompareOp::Never;
                case VK_COMPARE_OP_LESS:
                    return PSOCompareOp::Less;
                case VK_COMPARE_OP_EQUAL:
                    return PSOCompareOp::Equal;
                case VK_COMPARE_OP_LESS_OR_EQUAL:
                    return PSOCompareOp::LessOrEqual;
                case VK_COMPARE_OP_GREATER:
                    return PSOCompareOp::Greater;
                case VK_COMPARE_OP_NOT_EQUAL:
                    return PSOCompareOp::NotEqual;
                case VK_COMPARE_OP_GREATER_OR_EQUAL:
                    return PSOCompareOp::GreaterOrEqual;
                case VK_COMPARE_OP_ALWAYS:
                    return PSOCompareOp::Always;
                default:
                    ZENGINE_VALIDATE_ASSERT(false, "Unsupported Vulkan compare operation in PSO cache key")
                    return PSOCompareOp::Never;
            }
        }

        PSOBorderColor ToPSOBorderColor(VkBorderColor border_color)
        {
            switch (border_color)
            {
                case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
                    return PSOBorderColor::FloatTransparentBlack;
                case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
                    return PSOBorderColor::IntTransparentBlack;
                case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
                    return PSOBorderColor::FloatOpaqueBlack;
                case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
                    return PSOBorderColor::IntOpaqueBlack;
                case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
                    return PSOBorderColor::FloatOpaqueWhite;
                case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
                    return PSOBorderColor::IntOpaqueWhite;
                default:
                    ZENGINE_VALIDATE_ASSERT(false, "Unsupported Vulkan sampler border color in PSO cache key")
                    return PSOBorderColor::FloatTransparentBlack;
            }
        }

        VkFormat ResolveAttachmentFormat(Hardwares::VulkanDevice* device, Specifications::ImageFormat format)
        {
            if (format == Specifications::ImageFormat::FORMAT_FROM_DEVICE)
                return device->SurfaceFormat.format;
            if (format == Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE)
                return device->FindDepthFormat();

            constexpr uint32_t format_count = sizeof(Specifications::ImageFormatMap) / sizeof(Specifications::ImageFormatMap[0]);
            const uint32_t     format_index = static_cast<uint32_t>(format);
            ZENGINE_VALIDATE_ASSERT(format_index < format_count, "Unsupported image format in PSO compatibility render pass key")
            return Specifications::ImageFormatMap[format_index];
        }

        VkImageLayout ResolveReferenceLayout(Specifications::ImageLayout layout)
        {
            constexpr uint32_t layout_count = sizeof(Specifications::ImageLayoutMap) / sizeof(Specifications::ImageLayoutMap[0]);
            const uint32_t     layout_index = static_cast<uint32_t>(layout);
            ZENGINE_VALIDATE_ASSERT(layout_index < layout_count, "Unsupported image layout in PSO compatibility render pass key")
            return Specifications::ImageLayoutMap[layout_index];
        }

        void ValidateShaderStages(VkShaderStageFlags stages)
        {
            ZENGINE_VALIDATE_ASSERT(stages != 0, "PSO cache key requires at least one shader stage")
            ZENGINE_VALIDATE_ASSERT((stages & ~kSupportedShaderStages) == 0, "Unsupported Vulkan shader stage in PSO cache key")
        }

        const VkDescriptorSetLayoutBindingFlagsCreateInfo* GetBindingFlagsCreateInfo(const VkDescriptorSetLayoutCreateInfo& create_info)
        {
            if (create_info.pNext == nullptr)
                return nullptr;

            const auto* binding_flags = static_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo*>(create_info.pNext);
            ZENGINE_VALIDATE_ASSERT(binding_flags->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, "Unsupported descriptor-set layout pNext in PSO cache")
            ZENGINE_VALIDATE_ASSERT(binding_flags->pNext == nullptr, "Descriptor-set layout pNext chains are not supported by PSO cache")
            ZENGINE_VALIDATE_ASSERT(binding_flags->bindingCount == create_info.bindingCount, "Descriptor binding flag count must match binding count")
            return binding_flags;
        }

        void SortDescriptorBindings(PSODescriptorSetLayoutKey& key)
        {
            for (uint32_t i = 1; i < key.BindingCount; ++i)
            {
                PSODescriptorSetLayoutBindingKey current = key.Bindings[i];
                uint32_t                         index   = i;
                while (index > 0 && key.Bindings[index - 1].Binding > current.Binding)
                {
                    key.Bindings[index] = key.Bindings[index - 1];
                    --index;
                }
                key.Bindings[index] = current;
            }

            for (uint32_t i = 1; i < key.BindingCount; ++i)
                ZENGINE_VALIDATE_ASSERT(key.Bindings[i - 1].Binding != key.Bindings[i].Binding, "Descriptor-set layout bindings must be unique")
        }

        void SortPushConstantRanges(PSOPipelineLayoutKey& key)
        {
            for (uint32_t i = 1; i < key.PushConstantCount; ++i)
            {
                PSOPushConstantRangeKey current = key.PushConstants[i];
                uint32_t                index   = i;
                while (index > 0)
                {
                    const auto& previous = key.PushConstants[index - 1];
                    if (previous.Offset < current.Offset || (previous.Offset == current.Offset && previous.Size < current.Size) || (previous.Offset == current.Offset && previous.Size == current.Size && previous.ShaderStages <= current.ShaderStages))
                        break;
                    key.PushConstants[index] = previous;
                    --index;
                }
                key.PushConstants[index] = current;
            }
        }

        bool IsGraphicsShaderStage(VkShaderStageFlagBits stage)
        {
            switch (stage)
            {
                case VK_SHADER_STAGE_VERTEX_BIT:
                case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                case VK_SHADER_STAGE_GEOMETRY_BIT:
                case VK_SHADER_STAGE_FRAGMENT_BIT:
                    return true;
                default:
                    return false;
            }
        }

        void SortGraphicsShaderStages(PSOGraphicsPipelineKey& key)
        {
            for (uint32_t i = 1; i < key.ShaderStageCount; ++i)
            {
                PSOGraphicsShaderStageKey current = key.ShaderStages[i];
                uint32_t                  index   = i;
                while (index > 0 && key.ShaderStages[index - 1].Stage > current.Stage)
                {
                    key.ShaderStages[index] = key.ShaderStages[index - 1];
                    --index;
                }
                key.ShaderStages[index] = current;
            }

            for (uint32_t i = 1; i < key.ShaderStageCount; ++i)
                ZENGINE_VALIDATE_ASSERT(key.ShaderStages[i - 1].Stage != key.ShaderStages[i].Stage, "Graphics pipeline shader stages must be unique")
        }

        void SortVertexBindings(PSOGraphicsPipelineKey& key)
        {
            for (uint32_t i = 1; i < key.VertexBindingCount; ++i)
            {
                PSOVertexBindingKey current = key.VertexBindings[i];
                uint32_t            index   = i;
                while (index > 0 && key.VertexBindings[index - 1].Binding > current.Binding)
                {
                    key.VertexBindings[index] = key.VertexBindings[index - 1];
                    --index;
                }
                key.VertexBindings[index] = current;
            }

            for (uint32_t i = 1; i < key.VertexBindingCount; ++i)
                ZENGINE_VALIDATE_ASSERT(key.VertexBindings[i - 1].Binding != key.VertexBindings[i].Binding, "Graphics pipeline vertex bindings must be unique")
        }

        void SortVertexAttributes(PSOGraphicsPipelineKey& key)
        {
            for (uint32_t i = 1; i < key.VertexAttributeCount; ++i)
            {
                PSOVertexAttributeKey current = key.VertexAttributes[i];
                uint32_t              index   = i;
                while (index > 0 && key.VertexAttributes[index - 1].Location > current.Location)
                {
                    key.VertexAttributes[index] = key.VertexAttributes[index - 1];
                    --index;
                }
                key.VertexAttributes[index] = current;
            }

            for (uint32_t i = 1; i < key.VertexAttributeCount; ++i)
                ZENGINE_VALIDATE_ASSERT(key.VertexAttributes[i - 1].Location != key.VertexAttributes[i].Location, "Graphics pipeline vertex attribute locations must be unique")
        }

        PSOStencilOpStateKey MakeStencilOpStateKey(const VkStencilOpState& state)
        {
            return {
                .FailOp      = static_cast<uint32_t>(state.failOp),
                .PassOp      = static_cast<uint32_t>(state.passOp),
                .DepthFailOp = static_cast<uint32_t>(state.depthFailOp),
                .CompareOp   = static_cast<uint32_t>(state.compareOp),
                .CompareMask = state.compareMask,
                .WriteMask   = state.writeMask,
                .Reference   = state.reference,
            };
        }

        PSOColorBlendAttachmentKey MakeColorBlendAttachmentKey(const VkPipelineColorBlendAttachmentState& state)
        {
            return {
                .BlendEnable         = state.blendEnable,
                .SrcColorBlendFactor = static_cast<uint32_t>(state.srcColorBlendFactor),
                .DstColorBlendFactor = static_cast<uint32_t>(state.dstColorBlendFactor),
                .ColorBlendOp        = static_cast<uint32_t>(state.colorBlendOp),
                .SrcAlphaBlendFactor = static_cast<uint32_t>(state.srcAlphaBlendFactor),
                .DstAlphaBlendFactor = static_cast<uint32_t>(state.dstAlphaBlendFactor),
                .AlphaBlendOp        = static_cast<uint32_t>(state.alphaBlendOp),
                .ColorWriteMask      = state.colorWriteMask,
            };
        }
    } // namespace

    bool PSODescriptorSetLayoutBindingKey::operator==(const PSODescriptorSetLayoutBindingKey& other) const
    {
        return Binding == other.Binding && DescriptorCount == other.DescriptorCount && ShaderStages == other.ShaderStages && BindingFlags == other.BindingFlags && ImmutableSamplerListIdentity == other.ImmutableSamplerListIdentity && DescriptorKind == other.DescriptorKind;
    }

    bool PSOSamplerKey::operator==(const PSOSamplerKey& other) const
    {
        return CreateFlags == other.CreateFlags && MipLodBiasBits == other.MipLodBiasBits && MaxAnisotropyBits == other.MaxAnisotropyBits && MinLodBits == other.MinLodBits && MaxLodBits == other.MaxLodBits && CompareOp == other.CompareOp && BorderColor == other.BorderColor && MagFilter == other.MagFilter && MinFilter == other.MinFilter && MipmapMode == other.MipmapMode && AddressModeU == other.AddressModeU && AddressModeV == other.AddressModeV && AddressModeW == other.AddressModeW && AnisotropyEnabled == other.AnisotropyEnabled &&
               CompareEnabled == other.CompareEnabled && UnnormalizedCoords == other.UnnormalizedCoords;
    }

    bool PSOImmutableSamplerListKey::operator==(const PSOImmutableSamplerListKey& other) const
    {
        if (SamplerCount != other.SamplerCount)
            return false;
        for (uint32_t sampler_index = 0; sampler_index < SamplerCount; ++sampler_index)
            if (Samplers[sampler_index] != other.Samplers[sampler_index])
                return false;
        return true;
    }

    bool PSOCompatibilityAttachmentKey::operator==(const PSOCompatibilityAttachmentKey& other) const
    {
        return Format == other.Format && Samples == other.Samples && ReferenceLayout == other.ReferenceLayout;
    }

    bool PSOCompatibilityRenderPassKey::operator==(const PSOCompatibilityRenderPassKey& other) const
    {
        if (AttachmentCount != other.AttachmentCount || ColorAttachmentCount != other.ColorAttachmentCount || DepthAttachmentIndex != other.DepthAttachmentIndex || ViewMask != other.ViewMask)
            return false;
        for (uint32_t i = 0; i < AttachmentCount; ++i)
            if (!(Attachments[i] == other.Attachments[i]))
                return false;
        return true;
    }

    bool PSOPipelineLayoutReference::operator==(const PSOPipelineLayoutReference& other) const
    {
        return CachedIdentity == other.CachedIdentity && ExternalHandle == other.ExternalHandle;
    }

    bool PSOSpecializationValueKey::operator==(const PSOSpecializationValueKey& other) const
    {
        if (ConstantId != other.ConstantId || ByteCount != other.ByteCount)
            return false;
        return Helpers::secure_memcmp(Data, sizeof(Data), other.Data, sizeof(other.Data), ByteCount) == 0;
    }

    bool PSOSpecializationKey::operator==(const PSOSpecializationKey& other) const
    {
        if (EntryCount != other.EntryCount)
            return false;
        for (uint32_t entry_index = 0; entry_index < EntryCount; ++entry_index)
            if (!(Entries[entry_index] == other.Entries[entry_index]))
                return false;
        return true;
    }

    bool PSOComputeShaderKey::operator==(const PSOComputeShaderKey& other) const
    {
        return ModuleIdentity == other.ModuleIdentity && Generation == other.Generation && Stage == other.Stage && Specialization == other.Specialization;
    }

    bool PSOComputePipelineKey::operator==(const PSOComputePipelineKey& other) const
    {
        return CreateFlags == other.CreateFlags && Shader == other.Shader && Layout == other.Layout;
    }

    bool PSORenderPassReference::operator==(const PSORenderPassReference& other) const
    {
        return CachedIdentity == other.CachedIdentity && ExternalHandle == other.ExternalHandle;
    }

    bool PSOGraphicsShaderStageKey::operator==(const PSOGraphicsShaderStageKey& other) const
    {
        return ModuleIdentity == other.ModuleIdentity && Generation == other.Generation && Stage == other.Stage && Specialization == other.Specialization;
    }

    bool PSOVertexBindingKey::operator==(const PSOVertexBindingKey& other) const
    {
        return Binding == other.Binding && Stride == other.Stride && InputRate == other.InputRate;
    }

    bool PSOVertexAttributeKey::operator==(const PSOVertexAttributeKey& other) const
    {
        return Location == other.Location && Binding == other.Binding && Format == other.Format && Offset == other.Offset;
    }

    bool PSOStencilOpStateKey::operator==(const PSOStencilOpStateKey& other) const
    {
        return FailOp == other.FailOp && PassOp == other.PassOp && DepthFailOp == other.DepthFailOp && CompareOp == other.CompareOp && CompareMask == other.CompareMask && WriteMask == other.WriteMask && Reference == other.Reference;
    }

    bool PSOColorBlendAttachmentKey::operator==(const PSOColorBlendAttachmentKey& other) const
    {
        return BlendEnable == other.BlendEnable && SrcColorBlendFactor == other.SrcColorBlendFactor && DstColorBlendFactor == other.DstColorBlendFactor && ColorBlendOp == other.ColorBlendOp && SrcAlphaBlendFactor == other.SrcAlphaBlendFactor && DstAlphaBlendFactor == other.DstAlphaBlendFactor && AlphaBlendOp == other.AlphaBlendOp && ColorWriteMask == other.ColorWriteMask;
    }

    bool PSOGraphicsPipelineKey::operator==(const PSOGraphicsPipelineKey& other) const
    {
        if (CreateFlags != other.CreateFlags || ShaderStageCount != other.ShaderStageCount || Subpass != other.Subpass || VertexBindingCount != other.VertexBindingCount || VertexAttributeCount != other.VertexAttributeCount || InputAssemblyTopology != other.InputAssemblyTopology || PrimitiveRestartEnable != other.PrimitiveRestartEnable || ViewportCount != other.ViewportCount || ScissorCount != other.ScissorCount || RasterizationFlags != other.RasterizationFlags || DepthClampEnable != other.DepthClampEnable ||
            RasterizerDiscardEnable != other.RasterizerDiscardEnable || PolygonMode != other.PolygonMode || CullMode != other.CullMode || FrontFace != other.FrontFace || DepthBiasEnable != other.DepthBiasEnable || DepthBiasConstantFactorBits != other.DepthBiasConstantFactorBits || DepthBiasClampBits != other.DepthBiasClampBits || DepthBiasSlopeFactorBits != other.DepthBiasSlopeFactorBits || LineWidthBits != other.LineWidthBits || MultisampleFlags != other.MultisampleFlags || RasterizationSamples != other.RasterizationSamples ||
            SampleShadingEnable != other.SampleShadingEnable || MinSampleShadingBits != other.MinSampleShadingBits || AlphaToCoverageEnable != other.AlphaToCoverageEnable || AlphaToOneEnable != other.AlphaToOneEnable || HasDepthStencilState != other.HasDepthStencilState || DepthStencilFlags != other.DepthStencilFlags || DepthTestEnable != other.DepthTestEnable || DepthWriteEnable != other.DepthWriteEnable || DepthCompareOp != other.DepthCompareOp || DepthBoundsTestEnable != other.DepthBoundsTestEnable || StencilTestEnable != other.StencilTestEnable ||
            MinDepthBoundsBits != other.MinDepthBoundsBits || MaxDepthBoundsBits != other.MaxDepthBoundsBits || ColorBlendFlags != other.ColorBlendFlags || LogicOpEnable != other.LogicOpEnable || LogicOp != other.LogicOp || ColorBlendAttachmentCount != other.ColorBlendAttachmentCount || UsesDynamicRendering != other.UsesDynamicRendering || RenderingViewMask != other.RenderingViewMask || RenderingColorAttachmentCount != other.RenderingColorAttachmentCount || RenderingDepthAttachmentFormat != other.RenderingDepthAttachmentFormat ||
            RenderingStencilAttachmentFormat != other.RenderingStencilAttachmentFormat || !(Layout == other.Layout) || !(RenderPass == other.RenderPass) || !(FrontStencil == other.FrontStencil) || !(BackStencil == other.BackStencil))
            return false;

        for (uint32_t i = 0; i < 4; ++i)
            if (BlendConstantBits[i] != other.BlendConstantBits[i])
                return false;
        for (uint32_t i = 0; i < RenderingColorAttachmentCount; ++i)
            if (RenderingColorAttachmentFormats[i] != other.RenderingColorAttachmentFormats[i])
                return false;
        for (uint32_t i = 0; i < ShaderStageCount; ++i)
            if (!(ShaderStages[i] == other.ShaderStages[i]))
                return false;
        for (uint32_t i = 0; i < VertexBindingCount; ++i)
            if (!(VertexBindings[i] == other.VertexBindings[i]))
                return false;
        for (uint32_t i = 0; i < VertexAttributeCount; ++i)
            if (!(VertexAttributes[i] == other.VertexAttributes[i]))
                return false;
        for (uint32_t i = 0; i < ColorBlendAttachmentCount; ++i)
            if (!(ColorBlendAttachments[i] == other.ColorBlendAttachments[i]))
                return false;
        return true;
    }

    bool PSODescriptorSetLayoutKey::operator==(const PSODescriptorSetLayoutKey& other) const
    {
        if (CreateFlags != other.CreateFlags || BindingCount != other.BindingCount)
            return false;
        for (uint32_t i = 0; i < BindingCount; ++i)
            if (!(Bindings[i] == other.Bindings[i]))
                return false;
        return true;
    }

    bool PSOSetLayoutReference::operator==(const PSOSetLayoutReference& other) const
    {
        return CachedIdentity == other.CachedIdentity && ExternalHandle == other.ExternalHandle;
    }

    bool PSOPushConstantRangeKey::operator==(const PSOPushConstantRangeKey& other) const
    {
        return Offset == other.Offset && Size == other.Size && ShaderStages == other.ShaderStages;
    }

    bool PSOPipelineLayoutKey::operator==(const PSOPipelineLayoutKey& other) const
    {
        if (SetLayoutCount != other.SetLayoutCount || PushConstantCount != other.PushConstantCount)
            return false;
        for (uint32_t i = 0; i < SetLayoutCount; ++i)
            if (!(SetLayouts[i] == other.SetLayouts[i]))
                return false;
        for (uint32_t i = 0; i < PushConstantCount; ++i)
            if (!(PushConstants[i] == other.PushConstants[i]))
                return false;
        return true;
    }

    void PSOCache::DestroyGraphicsPipeline(void* context, GraphicsPipelineEntry& entry)
    {
        PSOCache* cache = static_cast<PSOCache*>(context);
        if (entry.Handle != VK_NULL_HANDLE)
            vkDestroyPipeline(cache->m_device->LogicalDevice, entry.Handle, nullptr);
    }

    void PSOCache::DestroyComputePipeline(void* context, ComputePipelineEntry& entry)
    {
        PSOCache* cache = static_cast<PSOCache*>(context);
        if (entry.Handle != VK_NULL_HANDLE)
            vkDestroyPipeline(cache->m_device->LogicalDevice, entry.Handle, nullptr);
    }

    void PSOCache::DestroyCompatibilityRenderPass(void* context, CompatibilityRenderPassEntry& entry)
    {
        PSOCache* cache = static_cast<PSOCache*>(context);
        if (entry.Handle != VK_NULL_HANDLE)
            vkDestroyRenderPass(cache->m_device->LogicalDevice, entry.Handle, nullptr);
    }

    void PSOCache::DestroyPipelineLayout(void* context, PipelineLayoutEntry& entry)
    {
        PSOCache* cache = static_cast<PSOCache*>(context);
        if (entry.Handle != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(cache->m_device->LogicalDevice, entry.Handle, nullptr);
    }

    void PSOCache::DestroyDescriptorSetLayout(void* context, DescriptorSetLayoutEntry& entry)
    {
        PSOCache* cache = static_cast<PSOCache*>(context);
        if (entry.Handle != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(cache->m_device->LogicalDevice, entry.Handle, nullptr);
    }

    void PSOCache::DestroySampler(void* context, SamplerEntry& entry)
    {
        PSOCache* cache = static_cast<PSOCache*>(context);
        if (entry.Handle != VK_NULL_HANDLE)
            vkDestroySampler(cache->m_device->LogicalDevice, entry.Handle, nullptr);
    }

    bool PSOCache::RemoveInvalidatedComputePipeline(void* context, const PSOComputePipelineKey& key, ComputePipelineEntry& entry)
    {
        const ShaderInvalidationContext& invalidation = *static_cast<const ShaderInvalidationContext*>(context);
        if (key.Shader.Generation != invalidation.Generation)
            return false;

        for (uint32_t module_index = 0; module_index < invalidation.ModuleCount; ++module_index)
        {
            if (key.Shader.ModuleIdentity != reinterpret_cast<uintptr_t>(invalidation.ShaderModules[module_index]))
                continue;

            if (entry.AsyncJobIndex != UINT32_MAX)
                invalidation.Cache->CancelAsyncPipelineJob(entry.AsyncJobIndex);
            invalidation.Cache->QueuePipelineWaiters(entry, VK_NULL_HANDLE);
            if (entry.State == PSOPipelineState::Ready)
                invalidation.Cache->RetirePipeline(entry.Handle);
            ++invalidation.Cache->m_telemetry.InvalidatedComputePipelines;
            return true;
        }
        return false;
    }

    bool PSOCache::RemoveInvalidatedGraphicsPipeline(void* context, const PSOGraphicsPipelineKey& key, GraphicsPipelineEntry& entry)
    {
        const ShaderInvalidationContext& invalidation = *static_cast<const ShaderInvalidationContext*>(context);
        for (uint32_t stage_index = 0; stage_index < key.ShaderStageCount; ++stage_index)
        {
            const PSOGraphicsShaderStageKey& stage = key.ShaderStages[stage_index];
            if (stage.Generation != invalidation.Generation)
                continue;

            for (uint32_t module_index = 0; module_index < invalidation.ModuleCount; ++module_index)
            {
                if (stage.ModuleIdentity != reinterpret_cast<uintptr_t>(invalidation.ShaderModules[module_index]))
                    continue;

                if (entry.AsyncJobIndex != UINT32_MAX)
                    invalidation.Cache->CancelAsyncPipelineJob(entry.AsyncJobIndex);
                invalidation.Cache->QueuePipelineWaiters(entry, VK_NULL_HANDLE);
                if (entry.State == PSOPipelineState::Ready)
                    invalidation.Cache->RetirePipeline(entry.Handle);
                ++invalidation.Cache->m_telemetry.InvalidatedGraphicsPipelines;
                return true;
            }
        }
        return false;
    }

    void PSOCache::FindDescriptorSetLayoutIdentity(void* context, const DescriptorSetLayoutEntry& entry)
    {
        DescriptorSetLayoutIdentityContext& lookup = *static_cast<DescriptorSetLayoutIdentityContext*>(context);
        if (entry.Handle == lookup.Handle)
            lookup.Identity = entry.Identity;
    }

    void PSOCache::FindImmutableSamplerListIdentity(void* context, const ImmutableSamplerListEntry& entry)
    {
        ImmutableSamplerListIdentityContext& lookup = *static_cast<ImmutableSamplerListIdentityContext*>(context);
        if (entry.Identity == lookup.Identity)
            lookup.Entry = &entry;
    }

    void PSOCache::FindPipelineLayoutIdentity(void* context, const PipelineLayoutEntry& entry)
    {
        PipelineLayoutIdentityContext& lookup = *static_cast<PipelineLayoutIdentityContext*>(context);
        if (entry.Handle == lookup.Handle)
            lookup.Identity = entry.Identity;
    }

    void PSOCache::FindCompatibilityRenderPassIdentity(void* context, const CompatibilityRenderPassEntry& entry)
    {
        CompatibilityRenderPassIdentityContext& lookup = *static_cast<CompatibilityRenderPassIdentityContext*>(context);
        if (entry.Handle == lookup.Handle)
            lookup.Identity = entry.Identity;
    }

    void PSOCache::AdjustComputePipelinePin(void* context, ComputePipelineEntry& entry)
    {
        const PipelinePinContext& pin = *static_cast<const PipelinePinContext*>(context);
        if (entry.Handle != pin.Handle)
            return;

        if (pin.Pin)
        {
            ZENGINE_VALIDATE_ASSERT(entry.PinCount < UINT32_MAX, "PSO compute pipeline pin count overflow")
            ++entry.PinCount;
            entry.LastUsedFrame = pin.LastUsedFrame;
        }
        else if (entry.PinCount > 0)
        {
            --entry.PinCount;
        }
    }

    void PSOCache::AdjustGraphicsPipelinePin(void* context, GraphicsPipelineEntry& entry)
    {
        const PipelinePinContext& pin = *static_cast<const PipelinePinContext*>(context);
        if (entry.Handle != pin.Handle)
            return;

        if (pin.Pin)
        {
            ZENGINE_VALIDATE_ASSERT(entry.PinCount < UINT32_MAX, "PSO graphics pipeline pin count overflow")
            ++entry.PinCount;
            entry.LastUsedFrame = pin.LastUsedFrame;
        }
        else if (entry.PinCount > 0)
        {
            --entry.PinCount;
        }
    }

    bool PSOCache::RemoveEvictedComputePipeline(void* context, const PSOComputePipelineKey& key, ComputePipelineEntry& entry)
    {
        (void) key;
        PipelineEvictionContext& eviction = *static_cast<PipelineEvictionContext*>(context);
        if (entry.State != PSOPipelineState::Ready || entry.PinCount != 0 || eviction.CurrentFrame < entry.LastUsedFrame || eviction.CurrentFrame - entry.LastUsedFrame < kPSOPipelineEvictionAge)
            return false;

        eviction.Cache->RetirePipeline(entry.Handle);
        ++eviction.Cache->m_telemetry.EvictedComputePipelines;
        return true;
    }

    bool PSOCache::RemoveEvictedGraphicsPipeline(void* context, const PSOGraphicsPipelineKey& key, GraphicsPipelineEntry& entry)
    {
        (void) key;
        PipelineEvictionContext& eviction = *static_cast<PipelineEvictionContext*>(context);
        if (entry.State != PSOPipelineState::Ready || entry.PinCount != 0 || eviction.CurrentFrame < entry.LastUsedFrame || eviction.CurrentFrame - entry.LastUsedFrame < kPSOPipelineEvictionAge)
            return false;

        eviction.Cache->RetirePipeline(entry.Handle);
        ++eviction.Cache->m_telemetry.EvictedGraphicsPipelines;
        return true;
    }

    void PSOCache::Initialize(Hardwares::VulkanDevice* device)
    {
        ZENGINE_VALIDATE_ASSERT(device != nullptr, "PSOCache::Initialize requires a Vulkan device")
        ZENGINE_VALIDATE_ASSERT(m_device == nullptr, "PSOCache::Initialize called twice")
        m_device                              = device;

        VkPipelineCacheCreateInfo create_info = {};
        create_info.sType                     = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        ZENGINE_VALIDATE_ASSERT(vkCreatePipelineCache(m_device->LogicalDevice, &create_info, nullptr, &m_driver_pipeline_cache) == VK_SUCCESS, "Failed to create driver pipeline cache")

        m_accept_async_jobs           = true;
        m_worker_pipeline_cache_count = std::min(m_device->WorkerThreadCount, kMaxPSOWorkerPipelineCaches);
        m_available_worker_pipeline_caches.value.store(0, std::memory_order_relaxed);
        m_active_async_jobs.value.store(0, std::memory_order_relaxed);
        for (uint32_t index = 0; index < m_worker_pipeline_cache_count; ++index)
        {
            ZENGINE_VALIDATE_ASSERT(vkCreatePipelineCache(m_device->LogicalDevice, &create_info, nullptr, &m_worker_pipeline_caches[index]) == VK_SUCCESS, "Failed to create asynchronous worker pipeline cache")
        }
        const uint32_t available_mask = m_worker_pipeline_cache_count == 0 ? 0U : (1U << m_worker_pipeline_cache_count) - 1U;
        m_available_worker_pipeline_caches.value.store(available_mask, std::memory_order_release);
    }

    void PSOCache::Shutdown()
    {
        if (!m_device)
            return;

        StopAsyncPipelineCompilation();
        DrainRetiredShaderModules();

        m_graphics_pipelines.ForEach(this, &DestroyGraphicsPipeline);
        m_compute_pipelines.ForEach(this, &DestroyComputePipeline);
        m_compatibility_render_passes.ForEach(this, &DestroyCompatibilityRenderPass);
        m_pipeline_layouts.ForEach(this, &DestroyPipelineLayout);
        m_descriptor_set_layouts.ForEach(this, &DestroyDescriptorSetLayout);
        m_samplers.ForEach(this, &DestroySampler);
        if (m_driver_pipeline_cache != VK_NULL_HANDLE)
        {
            vkDestroyPipelineCache(m_device->LogicalDevice, m_driver_pipeline_cache, nullptr);
            m_driver_pipeline_cache = VK_NULL_HANDLE;
        }
        for (uint32_t index = 0; index < m_worker_pipeline_cache_count; ++index)
        {
            if (m_worker_pipeline_caches[index] != VK_NULL_HANDLE)
            {
                vkDestroyPipelineCache(m_device->LogicalDevice, m_worker_pipeline_caches[index], nullptr);
                m_worker_pipeline_caches[index] = VK_NULL_HANDLE;
            }
        }
        m_compute_pipelines.Clear();
        m_graphics_pipelines.Clear();
        m_compatibility_render_passes.Clear();
        m_pipeline_layouts.Clear();
        m_descriptor_set_layouts.Clear();
        m_samplers.Clear();
        m_immutable_sampler_lists.Clear();
        m_next_layout_identity                    = 1;
        m_next_immutable_sampler_list_identity    = 1;
        m_next_pipeline_layout_identity           = 1;
        m_next_compatibility_render_pass_identity = 1;
        m_worker_pipeline_cache_count             = 0;
        m_available_worker_pipeline_caches.value.store(0, std::memory_order_relaxed);
        m_device = nullptr;
    }

    VkDescriptorSetLayout PSOCache::GetOrCreateDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& create_info)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        const PSODescriptorSetLayoutKey key  = MakeDescriptorSetLayoutKey(create_info);
        const uint64_t                  hash = HashDescriptorSetLayoutKey(key);
        bool                            created{};
        auto*                           entry = m_descriptor_set_layouts.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO descriptor-set layout collision bucket capacity exceeded")
        if (!created)
        {
            ++m_telemetry.DescriptorSetLayoutHits;
            return entry->Handle;
        }
        ++m_telemetry.DescriptorSetLayoutMisses;

        VkDescriptorSetLayoutBinding bindings[kMaxPSOLayoutBindings]      = {};
        VkDescriptorBindingFlags     binding_flags[kMaxPSOLayoutBindings] = {};
        bool                         has_binding_flags                    = false;
        for (uint32_t i = 0; i < key.BindingCount; ++i)
        {
            const auto&                      source             = key.Bindings[i];
            const ImmutableSamplerListEntry* immutable_samplers = source.ImmutableSamplerListIdentity == 0 ? nullptr : FindImmutableSamplerList(source.ImmutableSamplerListIdentity);
            ZENGINE_VALIDATE_ASSERT(source.ImmutableSamplerListIdentity == 0 || immutable_samplers != nullptr, "PSO immutable sampler list identity is invalid")
            bindings[i].binding             = source.Binding;
            bindings[i].descriptorCount     = source.DescriptorCount;
            bindings[i].descriptorType      = ToVulkanDescriptorType(source.DescriptorKind);
            bindings[i].stageFlags          = source.ShaderStages;
            bindings[i].pImmutableSamplers  = immutable_samplers ? immutable_samplers->Samplers : nullptr;
            binding_flags[i]                = source.BindingFlags;
            has_binding_flags              |= binding_flags[i] != 0;
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {};
        binding_flags_info.sType                                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        binding_flags_info.bindingCount                                = key.BindingCount;
        binding_flags_info.pBindingFlags                               = binding_flags;

        VkDescriptorSetLayoutCreateInfo canonical_create_info          = {};
        canonical_create_info.sType                                    = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        canonical_create_info.pNext                                    = has_binding_flags ? &binding_flags_info : nullptr;
        canonical_create_info.flags                                    = key.CreateFlags;
        canonical_create_info.bindingCount                             = key.BindingCount;
        canonical_create_info.pBindings                                = bindings;
        ZENGINE_VALIDATE_ASSERT(vkCreateDescriptorSetLayout(m_device->LogicalDevice, &canonical_create_info, nullptr, &entry->Handle) == VK_SUCCESS, "Failed to create cached descriptor set layout")
        entry->Identity = m_next_layout_identity++;
        return entry->Handle;
    }

    VkSampler PSOCache::GetOrCreateSampler(const VkSamplerCreateInfo& create_info)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        const PSOSamplerKey key  = MakeSamplerKey(create_info);
        const uint64_t      hash = HashSamplerKey(key);
        bool                created{};
        auto*               entry = m_samplers.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO sampler collision bucket capacity exceeded")
        if (!created)
        {
            ++m_telemetry.SamplerHits;
            return entry->Handle;
        }
        ++m_telemetry.SamplerMisses;

        ZENGINE_VALIDATE_ASSERT(vkCreateSampler(m_device->LogicalDevice, &create_info, nullptr, &entry->Handle) == VK_SUCCESS, "Failed to create cached sampler")
        return entry->Handle;
    }

    VkPipelineLayout PSOCache::GetOrCreatePipelineLayout(const VkPipelineLayoutCreateInfo& create_info)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        const PSOPipelineLayoutKey key  = MakePipelineLayoutKey(create_info);
        const uint64_t             hash = HashPipelineLayoutKey(key);
        bool                       created{};
        auto*                      entry = m_pipeline_layouts.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO pipeline-layout collision bucket capacity exceeded")
        if (!created)
        {
            ++m_telemetry.PipelineLayoutHits;
            return entry->Handle;
        }
        ++m_telemetry.PipelineLayoutMisses;

        ZENGINE_VALIDATE_ASSERT(create_info.sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, "Invalid pipeline-layout create info")
        ZENGINE_VALIDATE_ASSERT(create_info.pNext == nullptr, "Pipeline-layout pNext chains are not supported by PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.flags == 0, "Unsupported pipeline-layout create flags in PSO cache")
        ZENGINE_VALIDATE_ASSERT(vkCreatePipelineLayout(m_device->LogicalDevice, &create_info, nullptr, &entry->Handle) == VK_SUCCESS, "Failed to create cached pipeline layout")
        entry->Identity = m_next_pipeline_layout_identity++;
        return entry->Handle;
    }

    VkRenderPass PSOCache::GetOrCreateCompatibilityRenderPass(const RenderPasses::Attachment& attachment)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        return GetOrCreateCompatibilityRenderPass(MakeCompatibilityRenderPassKey(attachment));
    }

    VkRenderPass PSOCache::GetOrCreateCompatibilityRenderPass(const PSOCompatibilityRenderPassKey& key)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        const uint64_t hash = HashCompatibilityRenderPassKey(key);
        bool           created{};
        auto*          entry = m_compatibility_render_passes.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO compatibility render-pass collision bucket capacity exceeded")
        if (!created)
        {
            ++m_telemetry.CompatibilityRenderPassHits;
            return entry->Handle;
        }
        ++m_telemetry.CompatibilityRenderPassMisses;

        VkAttachmentDescription attachments[kMaxPSOCompatibilityAttachments] = {};
        VkAttachmentReference   colors[kMaxPSOCompatibilityAttachments]      = {};
        VkAttachmentReference   depth_reference                              = {.attachment = VK_ATTACHMENT_UNUSED, .layout = VK_IMAGE_LAYOUT_UNDEFINED};
        uint32_t                color_index                                  = 0;
        for (uint32_t i = 0; i < key.AttachmentCount; ++i)
        {
            const auto& source              = key.Attachments[i];
            auto&       destination         = attachments[i];
            destination.format              = static_cast<VkFormat>(source.Format);
            destination.samples             = static_cast<VkSampleCountFlagBits>(source.Samples);
            destination.loadOp              = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            destination.storeOp             = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            destination.stencilLoadOp       = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            destination.stencilStoreOp      = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            destination.initialLayout       = VK_IMAGE_LAYOUT_UNDEFINED;
            destination.finalLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

            VkAttachmentReference reference = {.attachment = i, .layout = static_cast<VkImageLayout>(source.ReferenceLayout)};
            if (i == key.DepthAttachmentIndex)
                depth_reference = reference;
            else
                colors[color_index++] = reference;
        }
        ZENGINE_VALIDATE_ASSERT(color_index == key.ColorAttachmentCount, "Invalid PSO compatibility render-pass color attachment count")

        VkSubpassDescription subpass       = {};
        subpass.pipelineBindPoint          = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount       = key.ColorAttachmentCount;
        subpass.pColorAttachments          = colors;
        subpass.pDepthStencilAttachment    = key.DepthAttachmentIndex == VK_ATTACHMENT_UNUSED ? nullptr : &depth_reference;

        VkRenderPassCreateInfo create_info = {};
        create_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create_info.attachmentCount        = key.AttachmentCount;
        create_info.pAttachments           = attachments;
        create_info.subpassCount           = 1;
        create_info.pSubpasses             = &subpass;
        ZENGINE_VALIDATE_ASSERT(vkCreateRenderPass(m_device->LogicalDevice, &create_info, nullptr, &entry->Handle) == VK_SUCCESS, "Failed to create PSO compatibility render pass")
        entry->Identity = m_next_compatibility_render_pass_identity++;
        return entry->Handle;
    }

    VkPipeline PSOCache::GetOrCreateComputePipeline(const VkComputePipelineCreateInfo& create_info, uint32_t shader_generation)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        const PSOComputePipelineKey key  = MakeComputePipelineKey(create_info, shader_generation);
        const uint64_t              hash = HashComputePipelineKey(key);
        bool                        created{};
        auto*                       entry = m_compute_pipelines.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO compute pipeline collision bucket capacity exceeded")
        if (!created && entry->State == PSOPipelineState::Ready)
        {
            ++m_telemetry.ComputePipelineHits;
            entry->LastUsedFrame = GetCurrentFrameMarker();
            return entry->Handle;
        }
        ++m_telemetry.ComputePipelineMisses;

        if (!created && entry->AsyncJobIndex != UINT32_MAX)
            CancelAsyncPipelineJob(entry->AsyncJobIndex);
        entry->State         = PSOPipelineState::Compiling;
        entry->Generation    = shader_generation;
        entry->AsyncJobIndex = UINT32_MAX;
        ZENGINE_VALIDATE_ASSERT(vkCreateComputePipelines(m_device->LogicalDevice, m_driver_pipeline_cache, 1, &create_info, nullptr, &entry->Handle) == VK_SUCCESS, "Failed to create cached compute pipeline")
        entry->State         = PSOPipelineState::Ready;
        entry->LastUsedFrame = GetCurrentFrameMarker();
        QueuePipelineWaiters(*entry, entry->Handle);
        DispatchPipelineNotifications();
        return entry->Handle;
    }

    VkPipeline PSOCache::GetOrCreateGraphicsPipeline(const VkGraphicsPipelineCreateInfo& create_info, uint32_t shader_generation)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        const PSOGraphicsPipelineKey key  = MakeGraphicsPipelineKey(create_info, shader_generation);
        const uint64_t               hash = HashGraphicsPipelineKey(key);
        bool                         created{};
        auto*                        entry = m_graphics_pipelines.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO graphics pipeline collision bucket capacity exceeded")
        if (!created && entry->State == PSOPipelineState::Ready)
        {
            ++m_telemetry.GraphicsPipelineHits;
            entry->LastUsedFrame = GetCurrentFrameMarker();
            return entry->Handle;
        }
        ++m_telemetry.GraphicsPipelineMisses;

        if (!created && entry->AsyncJobIndex != UINT32_MAX)
            CancelAsyncPipelineJob(entry->AsyncJobIndex);
        entry->State         = PSOPipelineState::Compiling;
        entry->Generation    = shader_generation;
        entry->AsyncJobIndex = UINT32_MAX;
        ZENGINE_VALIDATE_ASSERT(vkCreateGraphicsPipelines(m_device->LogicalDevice, m_driver_pipeline_cache, 1, &create_info, nullptr, &entry->Handle) == VK_SUCCESS, "Failed to create cached graphics pipeline")
        entry->State         = PSOPipelineState::Ready;
        entry->LastUsedFrame = GetCurrentFrameMarker();
        QueuePipelineWaiters(*entry, entry->Handle);
        DispatchPipelineNotifications();
        return entry->Handle;
    }

    VkPipeline PSOCache::RequestComputePipelineAsync(const VkComputePipelineCreateInfo& create_info, uint32_t shader_generation, void* callback_context, PSOPipelineReadyFn callback, bool critical)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        ++m_telemetry.AsyncPipelineRequests;
        if (!m_accept_async_jobs)
        {
            if (critical)
                return GetOrCreateComputePipeline(create_info, shader_generation);
            if (callback)
                callback(callback_context, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }

        const PSOComputePipelineKey key  = MakeComputePipelineKey(create_info, shader_generation);
        const uint64_t              hash = HashComputePipelineKey(key);
        bool                        created{};
        auto*                       entry = m_compute_pipelines.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO compute pipeline collision bucket capacity exceeded")

        const PSOPipelineWaiter waiter = {.Context = callback_context, .Callback = callback};
        if (!created && entry->State == PSOPipelineState::Ready)
        {
            ++m_telemetry.ComputePipelineHits;
            entry->LastUsedFrame = GetCurrentFrameMarker();
            if (callback)
                callback(callback_context, entry->Handle);
            return entry->Handle;
        }

        if (!created)
        {
            if (AppendWaiter(*entry, waiter))
                return VK_NULL_HANDLE;
            ++m_telemetry.AsyncPipelineWaiterOverflows;
            if (critical)
                return GetOrCreateComputePipeline(create_info, shader_generation);
            if (callback)
                callback(callback_context, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }

        const uint32_t job_index = AllocateAsyncPipelineJob();
        if (job_index == UINT32_MAX)
        {
            ++m_telemetry.AsyncPipelineJobOverflows;
            m_compute_pipelines.Erase(hash, key);
            if (critical)
                return GetOrCreateComputePipeline(create_info, shader_generation);
            if (callback)
                callback(callback_context, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }

        entry->State         = PSOPipelineState::Compiling;
        entry->Generation    = shader_generation;
        entry->AsyncJobIndex = job_index;
        (void) AppendWaiter(*entry, waiter);

        AsyncPipelineJob& job = m_async_jobs[job_index];
        job.Cache             = this;
        job.Kind              = AsyncPipelineKind::Compute;
        job.State             = AsyncPipelineJobState::Queued;
        job.Index             = job_index;
        job.Generation        = shader_generation;
        job.Hash              = hash;
        job.PipelineLayout    = create_info.layout;
        job.ComputeKey        = key;
        ++m_telemetry.AsyncPipelineJobs;
        DispatchQueuedAsyncPipelineJobs();
        return VK_NULL_HANDLE;
    }

    VkPipeline PSOCache::RequestGraphicsPipelineAsync(const VkGraphicsPipelineCreateInfo& create_info, uint32_t shader_generation, void* callback_context, PSOPipelineReadyFn callback, bool critical)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        ++m_telemetry.AsyncPipelineRequests;
        if (!m_accept_async_jobs)
        {
            if (critical)
                return GetOrCreateGraphicsPipeline(create_info, shader_generation);
            if (callback)
                callback(callback_context, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }

        const PSOGraphicsPipelineKey key  = MakeGraphicsPipelineKey(create_info, shader_generation);
        const uint64_t               hash = HashGraphicsPipelineKey(key);
        bool                         created{};
        auto*                        entry = m_graphics_pipelines.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO graphics pipeline collision bucket capacity exceeded")

        const PSOPipelineWaiter waiter = {.Context = callback_context, .Callback = callback};
        if (!created && entry->State == PSOPipelineState::Ready)
        {
            ++m_telemetry.GraphicsPipelineHits;
            entry->LastUsedFrame = GetCurrentFrameMarker();
            if (callback)
                callback(callback_context, entry->Handle);
            return entry->Handle;
        }

        if (!created)
        {
            if (AppendWaiter(*entry, waiter))
                return VK_NULL_HANDLE;
            ++m_telemetry.AsyncPipelineWaiterOverflows;
            if (critical)
                return GetOrCreateGraphicsPipeline(create_info, shader_generation);
            if (callback)
                callback(callback_context, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }

        const uint32_t job_index = AllocateAsyncPipelineJob();
        if (job_index == UINT32_MAX)
        {
            ++m_telemetry.AsyncPipelineJobOverflows;
            m_graphics_pipelines.Erase(hash, key);
            if (critical)
                return GetOrCreateGraphicsPipeline(create_info, shader_generation);
            if (callback)
                callback(callback_context, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }

        entry->State         = PSOPipelineState::Compiling;
        entry->Generation    = shader_generation;
        entry->AsyncJobIndex = job_index;
        (void) AppendWaiter(*entry, waiter);

        AsyncPipelineJob& job = m_async_jobs[job_index];
        job.Cache             = this;
        job.Kind              = AsyncPipelineKind::Graphics;
        job.State             = AsyncPipelineJobState::Queued;
        job.Index             = job_index;
        job.Generation        = shader_generation;
        job.Hash              = hash;
        job.PipelineLayout    = create_info.layout;
        job.RenderPass        = create_info.renderPass;
        job.GraphicsKey       = key;
        ++m_telemetry.AsyncPipelineJobs;
        DispatchQueuedAsyncPipelineJobs();
        return VK_NULL_HANDLE;
    }

    void PSOCache::FlushAsyncPipelineJobs()
    {
        if (!m_device)
            return;

        AsyncPipelineCompletion completion = {};
        while (m_async_completions.pop(completion))
            PublishAsyncCompletion(completion);

        DrainRetiredShaderModules();
        DispatchQueuedAsyncPipelineJobs();
        DispatchPipelineNotifications();
    }

    void PSOCache::StopAsyncPipelineCompilation()
    {
        if (!m_device)
            return;

        m_accept_async_jobs = false;
        for (uint32_t index = 0; index < kMaxPSOAsyncJobs; ++index)
        {
            AsyncPipelineJob& job = m_async_jobs[index];
            if (job.State == AsyncPipelineJobState::Free)
                continue;

            if (job.Kind == AsyncPipelineKind::Compute)
            {
                if (ComputePipelineEntry* entry = m_compute_pipelines.Find(job.Hash, job.ComputeKey))
                {
                    if (entry->State == PSOPipelineState::Compiling && entry->AsyncJobIndex == job.Index)
                    {
                        QueuePipelineWaiters(*entry, VK_NULL_HANDLE);
                        m_compute_pipelines.Erase(job.Hash, job.ComputeKey);
                    }
                }
            }
            else if (GraphicsPipelineEntry* entry = m_graphics_pipelines.Find(job.Hash, job.GraphicsKey))
            {
                if (entry->State == PSOPipelineState::Compiling && entry->AsyncJobIndex == job.Index)
                {
                    QueuePipelineWaiters(*entry, VK_NULL_HANDLE);
                    m_graphics_pipelines.Erase(job.Hash, job.GraphicsKey);
                }
            }

            if (job.State == AsyncPipelineJobState::Queued)
                job = {};
            else
                job.State = AsyncPipelineJobState::Cancelled;
        }

        // Existing callbacks may belong to passes already being destroyed during shutdown.
        // Discarding them is safe because device teardown has no live pipeline consumer.
        m_pending_notification_count = 0;
        while (m_active_async_jobs.value.load(std::memory_order_acquire) != 0)
        {
            AsyncPipelineCompletion completion = {};
            while (m_async_completions.pop(completion))
                PublishAsyncCompletion(completion);
            std::this_thread::yield();
        }
        FlushAsyncPipelineJobs();
        m_pending_notification_count = 0;
    }

    void PSOCache::RetireShaderModule(VkShaderModule shader_module)
    {
        if (shader_module == VK_NULL_HANDLE)
            return;
        if (!IsShaderModulePinnedByAsyncJob(shader_module))
        {
            RetireShaderModuleNow(shader_module);
            return;
        }

        for (uint32_t index = 0; index < m_retired_shader_module_count; ++index)
        {
            if (m_retired_shader_modules[index].Handle == shader_module)
                return;
        }
        ZENGINE_VALIDATE_ASSERT(m_retired_shader_module_count < kMaxPSORetiredShaderModules, "PSO retired shader-module capacity exceeded")
        m_retired_shader_modules[m_retired_shader_module_count++].Handle = shader_module;
    }

    void PSOCache::RetireShaderModuleNow(VkShaderModule shader_module) const
    {
        if (shader_module == VK_NULL_HANDLE)
            return;

        Hardwares::DeferredFreeEntry entry = {};
        entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
        entry.Data.Vk                      = {reinterpret_cast<void*>(shader_module), DeviceResourceType::SHADERMODULE, nullptr};
        m_device->DeferFree(entry);
    }

    bool PSOCache::IsShaderModulePinnedByAsyncJob(VkShaderModule shader_module) const
    {
        const uint64_t module_identity = reinterpret_cast<uintptr_t>(shader_module);
        for (uint32_t index = 0; index < kMaxPSOAsyncJobs; ++index)
        {
            const AsyncPipelineJob& job = m_async_jobs[index];
            if (job.State == AsyncPipelineJobState::Free)
                continue;

            if (job.Kind == AsyncPipelineKind::Compute)
            {
                if (job.ComputeKey.Shader.ModuleIdentity == module_identity)
                    return true;
                continue;
            }

            for (uint32_t stage_index = 0; stage_index < job.GraphicsKey.ShaderStageCount; ++stage_index)
            {
                if (job.GraphicsKey.ShaderStages[stage_index].ModuleIdentity == module_identity)
                    return true;
            }
        }
        return false;
    }

    void PSOCache::DrainRetiredShaderModules()
    {
        uint32_t index = 0;
        while (index < m_retired_shader_module_count)
        {
            const VkShaderModule shader_module = m_retired_shader_modules[index].Handle;
            if (IsShaderModulePinnedByAsyncJob(shader_module))
            {
                ++index;
                continue;
            }

            RetireShaderModuleNow(shader_module);
            m_retired_shader_modules[index]                         = m_retired_shader_modules[--m_retired_shader_module_count];
            m_retired_shader_modules[m_retired_shader_module_count] = {};
        }
    }

    bool PSOCache::TryCheckoutWorkerPipelineCache(uint32_t* worker_cache_index)
    {
        ZENGINE_VALIDATE_ASSERT(worker_cache_index != nullptr, "PSOCache worker-cache output is null")
        uint32_t available = m_available_worker_pipeline_caches.value.load(std::memory_order_acquire);
        while (available != 0)
        {
            for (uint32_t index = 0; index < m_worker_pipeline_cache_count; ++index)
            {
                const uint32_t bit = 1U << index;
                if ((available & bit) == 0)
                    continue;

                const uint32_t desired = available & ~bit;
                if (m_available_worker_pipeline_caches.value.compare_exchange_weak(available, desired, std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    *worker_cache_index = index;
                    return true;
                }
                break;
            }
        }
        return false;
    }

    void PSOCache::ReturnWorkerPipelineCache(uint32_t worker_cache_index)
    {
        ZENGINE_VALIDATE_ASSERT(worker_cache_index < m_worker_pipeline_cache_count, "Invalid PSO worker pipeline-cache index")
        m_available_worker_pipeline_caches.value.fetch_or(1U << worker_cache_index, std::memory_order_release);
    }

    uint32_t PSOCache::AllocateAsyncPipelineJob()
    {
        for (uint32_t index = 0; index < kMaxPSOAsyncJobs; ++index)
        {
            if (m_async_jobs[index].State == AsyncPipelineJobState::Free)
                return index;
        }
        return UINT32_MAX;
    }

    void PSOCache::CancelAsyncPipelineJob(uint32_t job_index)
    {
        if (job_index >= kMaxPSOAsyncJobs)
            return;

        AsyncPipelineJob& job = m_async_jobs[job_index];
        if (job.State == AsyncPipelineJobState::Queued)
            job = {};
        else if (job.State == AsyncPipelineJobState::Running)
            job.State = AsyncPipelineJobState::Cancelled;
    }

    void PSOCache::DispatchQueuedAsyncPipelineJobs()
    {
        if (!m_accept_async_jobs || !Helpers::ThreadPoolHelper::IsInitialized())
            return;

        for (uint32_t index = 0; index < kMaxPSOAsyncJobs; ++index)
        {
            AsyncPipelineJob& job = m_async_jobs[index];
            if (job.State != AsyncPipelineJobState::Queued)
                continue;

            uint32_t worker_cache_index = UINT32_MAX;
            if (!TryCheckoutWorkerPipelineCache(&worker_cache_index))
                return;

            job.WorkerCacheIndex = worker_cache_index;
            job.State            = AsyncPipelineJobState::Running;
            m_active_async_jobs.value.fetch_add(1, std::memory_order_release);
            Helpers::ThreadPoolHelper::Submit(&job, &PSOCache::CompileAsyncPipelineJob);
        }
    }

    void PSOCache::QueuePipelineNotification(const PSOPipelineWaiter& waiter, VkPipeline pipeline)
    {
        if (!m_accept_async_jobs || !waiter.Callback)
            return;

        ZENGINE_VALIDATE_ASSERT(m_pending_notification_count < kMaxPSOAsyncJobs * kMaxPSOAsyncWaiters, "PSO pipeline notification capacity exceeded")
        m_pending_notifications[m_pending_notification_count++] = {.Waiter = waiter, .Pipeline = pipeline};
    }

    void PSOCache::QueuePipelineWaiters(ComputePipelineEntry& entry, VkPipeline pipeline)
    {
        for (uint32_t index = 0; index < entry.WaiterCount; ++index)
            QueuePipelineNotification(entry.Waiters[index], pipeline);
        entry.WaiterCount = 0;
    }

    void PSOCache::QueuePipelineWaiters(GraphicsPipelineEntry& entry, VkPipeline pipeline)
    {
        for (uint32_t index = 0; index < entry.WaiterCount; ++index)
            QueuePipelineNotification(entry.Waiters[index], pipeline);
        entry.WaiterCount = 0;
    }

    void PSOCache::DispatchPipelineNotifications()
    {
        const uint32_t notification_count = m_pending_notification_count;
        m_pending_notification_count      = 0;
        for (uint32_t index = 0; index < notification_count; ++index)
        {
            const PendingPipelineNotification notification = m_pending_notifications[index];
            m_pending_notifications[index]                 = {};
            notification.Waiter.Callback(notification.Waiter.Context, notification.Pipeline);
        }
    }

    bool PSOCache::AppendWaiter(ComputePipelineEntry& entry, const PSOPipelineWaiter& waiter)
    {
        if (!waiter.Callback)
            return true;
        if (entry.WaiterCount == kMaxPSOAsyncWaiters)
            return false;
        entry.Waiters[entry.WaiterCount++] = waiter;
        return true;
    }

    bool PSOCache::AppendWaiter(GraphicsPipelineEntry& entry, const PSOPipelineWaiter& waiter)
    {
        if (!waiter.Callback)
            return true;
        if (entry.WaiterCount == kMaxPSOAsyncWaiters)
            return false;
        entry.Waiters[entry.WaiterCount++] = waiter;
        return true;
    }

    void PSOCache::PublishAsyncCompletion(const AsyncPipelineCompletion& completion)
    {
        ZENGINE_VALIDATE_ASSERT(completion.JobIndex < kMaxPSOAsyncJobs, "Invalid PSO asynchronous completion job index")
        AsyncPipelineJob& job = m_async_jobs[completion.JobIndex];
        ZENGINE_VALIDATE_ASSERT(job.State == AsyncPipelineJobState::Running || job.State == AsyncPipelineJobState::Cancelled, "PSO asynchronous completion has no active job")
        ZENGINE_VALIDATE_ASSERT(job.WorkerCacheIndex == completion.WorkerCacheIndex, "PSO asynchronous completion returned the wrong worker cache")

        ZENGINE_VALIDATE_ASSERT(vkMergePipelineCaches(m_device->LogicalDevice, m_driver_pipeline_cache, 1, &m_worker_pipeline_caches[completion.WorkerCacheIndex]) == VK_SUCCESS, "Failed to merge asynchronous worker pipeline cache")
        ReturnWorkerPipelineCache(completion.WorkerCacheIndex);

        if (job.Kind == AsyncPipelineKind::Compute)
        {
            ComputePipelineEntry* entry   = m_compute_pipelines.Find(job.Hash, job.ComputeKey);
            const bool            publish = job.State == AsyncPipelineJobState::Running && completion.Result == VK_SUCCESS && completion.Pipeline != VK_NULL_HANDLE && entry != nullptr && entry->State == PSOPipelineState::Compiling && entry->Generation == job.Generation && entry->AsyncJobIndex == job.Index;
            if (publish)
            {
                entry->Handle        = completion.Pipeline;
                entry->LastUsedFrame = GetCurrentFrameMarker();
                entry->State         = PSOPipelineState::Ready;
                entry->AsyncJobIndex = UINT32_MAX;
                QueuePipelineWaiters(*entry, completion.Pipeline);
                ++m_telemetry.AsyncPipelinePublishes;
            }
            else
            {
                if (entry && entry->State == PSOPipelineState::Compiling && entry->AsyncJobIndex == job.Index)
                {
                    QueuePipelineWaiters(*entry, VK_NULL_HANDLE);
                    m_compute_pipelines.Erase(job.Hash, job.ComputeKey);
                }
                RetirePipeline(completion.Pipeline);
                ++m_telemetry.AsyncPipelineStaleCompletions;
            }
        }
        else
        {
            GraphicsPipelineEntry* entry   = m_graphics_pipelines.Find(job.Hash, job.GraphicsKey);
            const bool             publish = job.State == AsyncPipelineJobState::Running && completion.Result == VK_SUCCESS && completion.Pipeline != VK_NULL_HANDLE && entry != nullptr && entry->State == PSOPipelineState::Compiling && entry->Generation == job.Generation && entry->AsyncJobIndex == job.Index;
            if (publish)
            {
                entry->Handle        = completion.Pipeline;
                entry->LastUsedFrame = GetCurrentFrameMarker();
                entry->State         = PSOPipelineState::Ready;
                entry->AsyncJobIndex = UINT32_MAX;
                QueuePipelineWaiters(*entry, completion.Pipeline);
                ++m_telemetry.AsyncPipelinePublishes;
            }
            else
            {
                if (entry && entry->State == PSOPipelineState::Compiling && entry->AsyncJobIndex == job.Index)
                {
                    QueuePipelineWaiters(*entry, VK_NULL_HANDLE);
                    m_graphics_pipelines.Erase(job.Hash, job.GraphicsKey);
                }
                RetirePipeline(completion.Pipeline);
                ++m_telemetry.AsyncPipelineStaleCompletions;
            }
        }
        job = {};
    }

    void PSOCache::CompileAsyncPipelineJob(void* context)
    {
        AsyncPipelineJob* job = static_cast<AsyncPipelineJob*>(context);
        ZENGINE_VALIDATE_ASSERT(job != nullptr && job->Cache != nullptr, "PSO asynchronous worker job is invalid")

        PSOCache*                     cache        = job->Cache;
        VkPipeline                    pipeline     = VK_NULL_HANDLE;
        const VkPipelineCache         worker_cache = cache->m_worker_pipeline_caches[job->WorkerCacheIndex];
        const VkResult                result       = job->Kind == AsyncPipelineKind::Compute ? CompileAsyncComputePipeline(*job, cache->m_device->LogicalDevice, worker_cache, &pipeline) : CompileAsyncGraphicsPipeline(*job, cache->m_device->LogicalDevice, worker_cache, &pipeline);
        const AsyncPipelineCompletion completion   = {
            .JobIndex         = job->Index,
            .WorkerCacheIndex = job->WorkerCacheIndex,
            .Pipeline         = pipeline,
            .Result           = result,
        };

        while (!cache->m_async_completions.push(completion))
            std::this_thread::yield();
        cache->m_active_async_jobs.value.fetch_sub(1, std::memory_order_release);
    }

    VkResult PSOCache::CompileAsyncComputePipeline(const AsyncPipelineJob& job, VkDevice device, VkPipelineCache worker_cache, VkPipeline* pipeline)
    {
        ZENGINE_VALIDATE_ASSERT(pipeline != nullptr, "PSO asynchronous compute pipeline output is null")
        *pipeline                                                                                                    = VK_NULL_HANDLE;

        VkSpecializationMapEntry specialization_entries[kMaxPSOSpecializationEntries]                                = {};
        uint8_t                  specialization_data[kMaxPSOSpecializationEntries * kMaxPSOSpecializationValueBytes] = {};
        VkSpecializationInfo     specialization_info                                                                 = {};
        BuildSpecializationInfo(job.ComputeKey.Shader.Specialization, specialization_entries, specialization_data, &specialization_info);

        VkComputePipelineCreateInfo create_info = {};
        create_info.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        create_info.flags                       = job.ComputeKey.CreateFlags;
        create_info.stage.sType                 = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        create_info.stage.stage                 = VK_SHADER_STAGE_COMPUTE_BIT;
        create_info.stage.module                = reinterpret_cast<VkShaderModule>(job.ComputeKey.Shader.ModuleIdentity);
        create_info.stage.pName                 = "main";
        create_info.stage.pSpecializationInfo   = job.ComputeKey.Shader.Specialization.EntryCount == 0 ? nullptr : &specialization_info;
        create_info.layout                      = job.PipelineLayout;
        create_info.basePipelineIndex           = -1;
        return vkCreateComputePipelines(device, worker_cache, 1, &create_info, nullptr, pipeline);
    }

    VkResult PSOCache::CompileAsyncGraphicsPipeline(const AsyncPipelineJob& job, VkDevice device, VkPipelineCache worker_cache, VkPipeline* pipeline)
    {
        ZENGINE_VALIDATE_ASSERT(pipeline != nullptr, "PSO asynchronous graphics pipeline output is null")
        *pipeline                                                                                                                                        = VK_NULL_HANDLE;
        const PSOGraphicsPipelineKey&   key                                                                                                              = job.GraphicsKey;

        VkPipelineShaderStageCreateInfo stages[kMaxPSOGraphicsShaderStages]                                                                              = {};
        VkSpecializationMapEntry        specialization_entries[kMaxPSOGraphicsShaderStages][kMaxPSOSpecializationEntries]                                = {};
        uint8_t                         specialization_data[kMaxPSOGraphicsShaderStages][kMaxPSOSpecializationEntries * kMaxPSOSpecializationValueBytes] = {};
        VkSpecializationInfo            specialization_infos[kMaxPSOGraphicsShaderStages]                                                                = {};
        for (uint32_t index = 0; index < key.ShaderStageCount; ++index)
        {
            stages[index].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[index].stage  = static_cast<VkShaderStageFlagBits>(key.ShaderStages[index].Stage);
            stages[index].module = reinterpret_cast<VkShaderModule>(key.ShaderStages[index].ModuleIdentity);
            stages[index].pName  = "main";
            BuildSpecializationInfo(key.ShaderStages[index].Specialization, specialization_entries[index], specialization_data[index], &specialization_infos[index]);
            stages[index].pSpecializationInfo = key.ShaderStages[index].Specialization.EntryCount == 0 ? nullptr : &specialization_infos[index];
        }

        VkVertexInputBindingDescription vertex_bindings[kMaxPSOVertexBindings] = {};
        for (uint32_t index = 0; index < key.VertexBindingCount; ++index)
        {
            vertex_bindings[index] = {
                .binding   = key.VertexBindings[index].Binding,
                .stride    = key.VertexBindings[index].Stride,
                .inputRate = static_cast<VkVertexInputRate>(key.VertexBindings[index].InputRate),
            };
        }
        VkVertexInputAttributeDescription vertex_attributes[kMaxPSOVertexAttributes] = {};
        for (uint32_t index = 0; index < key.VertexAttributeCount; ++index)
        {
            vertex_attributes[index] = {
                .location = key.VertexAttributes[index].Location,
                .binding  = key.VertexAttributes[index].Binding,
                .format   = static_cast<VkFormat>(key.VertexAttributes[index].Format),
                .offset   = key.VertexAttributes[index].Offset,
            };
        }

        VkPipelineVertexInputStateCreateInfo vertex_input                                         = {};
        vertex_input.sType                                                                        = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount                                                = key.VertexBindingCount;
        vertex_input.pVertexBindingDescriptions                                                   = key.VertexBindingCount == 0 ? nullptr : vertex_bindings;
        vertex_input.vertexAttributeDescriptionCount                                              = key.VertexAttributeCount;
        vertex_input.pVertexAttributeDescriptions                                                 = key.VertexAttributeCount == 0 ? nullptr : vertex_attributes;

        VkPipelineInputAssemblyStateCreateInfo input_assembly                                     = {};
        input_assembly.sType                                                                      = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology                                                                   = static_cast<VkPrimitiveTopology>(key.InputAssemblyTopology);
        input_assembly.primitiveRestartEnable                                                     = key.PrimitiveRestartEnable;

        VkPipelineViewportStateCreateInfo viewport                                                = {};
        viewport.sType                                                                            = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount                                                                    = key.ViewportCount;
        viewport.scissorCount                                                                     = key.ScissorCount;

        VkPipelineRasterizationStateCreateInfo rasterization                                      = {};
        rasterization.sType                                                                       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.flags                                                                       = key.RasterizationFlags;
        rasterization.depthClampEnable                                                            = key.DepthClampEnable;
        rasterization.rasterizerDiscardEnable                                                     = key.RasterizerDiscardEnable;
        rasterization.polygonMode                                                                 = static_cast<VkPolygonMode>(key.PolygonMode);
        rasterization.cullMode                                                                    = key.CullMode;
        rasterization.frontFace                                                                   = static_cast<VkFrontFace>(key.FrontFace);
        rasterization.depthBiasEnable                                                             = key.DepthBiasEnable;
        rasterization.depthBiasConstantFactor                                                     = std::bit_cast<float>(key.DepthBiasConstantFactorBits);
        rasterization.depthBiasClamp                                                              = std::bit_cast<float>(key.DepthBiasClampBits);
        rasterization.depthBiasSlopeFactor                                                        = std::bit_cast<float>(key.DepthBiasSlopeFactorBits);
        rasterization.lineWidth                                                                   = std::bit_cast<float>(key.LineWidthBits);

        VkPipelineMultisampleStateCreateInfo multisample                                          = {};
        multisample.sType                                                                         = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.flags                                                                         = key.MultisampleFlags;
        multisample.rasterizationSamples                                                          = static_cast<VkSampleCountFlagBits>(key.RasterizationSamples);
        multisample.sampleShadingEnable                                                           = key.SampleShadingEnable;
        multisample.minSampleShading                                                              = std::bit_cast<float>(key.MinSampleShadingBits);
        multisample.alphaToCoverageEnable                                                         = key.AlphaToCoverageEnable;
        multisample.alphaToOneEnable                                                              = key.AlphaToOneEnable;

        VkPipelineDepthStencilStateCreateInfo depth_stencil                                       = {};
        depth_stencil.sType                                                                       = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.flags                                                                       = key.DepthStencilFlags;
        depth_stencil.depthTestEnable                                                             = key.DepthTestEnable;
        depth_stencil.depthWriteEnable                                                            = key.DepthWriteEnable;
        depth_stencil.depthCompareOp                                                              = static_cast<VkCompareOp>(key.DepthCompareOp);
        depth_stencil.depthBoundsTestEnable                                                       = key.DepthBoundsTestEnable;
        depth_stencil.stencilTestEnable                                                           = key.StencilTestEnable;
        depth_stencil.front                                                                       = MakeVulkanStencilState(key.FrontStencil);
        depth_stencil.back                                                                        = MakeVulkanStencilState(key.BackStencil);
        depth_stencil.minDepthBounds                                                              = std::bit_cast<float>(key.MinDepthBoundsBits);
        depth_stencil.maxDepthBounds                                                              = std::bit_cast<float>(key.MaxDepthBoundsBits);

        VkPipelineColorBlendAttachmentState color_blend_attachments[kMaxPSOColorBlendAttachments] = {};
        for (uint32_t index = 0; index < key.ColorBlendAttachmentCount; ++index)
        {
            const PSOColorBlendAttachmentKey& source = key.ColorBlendAttachments[index];
            color_blend_attachments[index]           = {
                .blendEnable         = source.BlendEnable,
                .srcColorBlendFactor = static_cast<VkBlendFactor>(source.SrcColorBlendFactor),
                .dstColorBlendFactor = static_cast<VkBlendFactor>(source.DstColorBlendFactor),
                .colorBlendOp        = static_cast<VkBlendOp>(source.ColorBlendOp),
                .srcAlphaBlendFactor = static_cast<VkBlendFactor>(source.SrcAlphaBlendFactor),
                .dstAlphaBlendFactor = static_cast<VkBlendFactor>(source.DstAlphaBlendFactor),
                .alphaBlendOp        = static_cast<VkBlendOp>(source.AlphaBlendOp),
                .colorWriteMask      = source.ColorWriteMask,
            };
        }
        VkPipelineColorBlendAttachmentState dummy_attachment = {};
        VkPipelineColorBlendStateCreateInfo color_blend      = {};
        color_blend.sType                                    = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.flags                                    = key.ColorBlendFlags;
        color_blend.logicOpEnable                            = key.LogicOpEnable;
        color_blend.logicOp                                  = static_cast<VkLogicOp>(key.LogicOp);
        color_blend.attachmentCount                          = key.ColorBlendAttachmentCount;
        color_blend.pAttachments                             = key.ColorBlendAttachmentCount == 0 ? &dummy_attachment : color_blend_attachments;
        for (uint32_t index = 0; index < 4; ++index)
            color_blend.blendConstants[index] = std::bit_cast<float>(key.BlendConstantBits[index]);

        VkDynamicState                   dynamic_states[]                                   = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state                                      = {};
        dynamic_state.sType                                                                 = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount                                                     = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
        dynamic_state.pDynamicStates                                                        = dynamic_states;

        VkPipelineRenderingCreateInfo rendering_info                                        = {};
        VkFormat                      rendering_color_formats[kMaxPSOColorBlendAttachments] = {};
        if (key.UsesDynamicRendering == VK_TRUE)
        {
            for (uint32_t index = 0; index < key.RenderingColorAttachmentCount; ++index)
                rendering_color_formats[index] = static_cast<VkFormat>(key.RenderingColorAttachmentFormats[index]);
            rendering_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.viewMask                = key.RenderingViewMask;
            rendering_info.colorAttachmentCount    = key.RenderingColorAttachmentCount;
            rendering_info.pColorAttachmentFormats = key.RenderingColorAttachmentCount == 0 ? nullptr : rendering_color_formats;
            rendering_info.depthAttachmentFormat   = static_cast<VkFormat>(key.RenderingDepthAttachmentFormat);
            rendering_info.stencilAttachmentFormat = static_cast<VkFormat>(key.RenderingStencilAttachmentFormat);
        }

        VkGraphicsPipelineCreateInfo create_info = {};
        create_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        create_info.flags                        = key.CreateFlags;
        create_info.stageCount                   = key.ShaderStageCount;
        create_info.pStages                      = stages;
        create_info.pVertexInputState            = &vertex_input;
        create_info.pInputAssemblyState          = &input_assembly;
        create_info.pViewportState               = &viewport;
        create_info.pRasterizationState          = &rasterization;
        create_info.pMultisampleState            = &multisample;
        create_info.pDepthStencilState           = key.HasDepthStencilState == VK_TRUE ? &depth_stencil : nullptr;
        create_info.pColorBlendState             = &color_blend;
        create_info.pDynamicState                = &dynamic_state;
        create_info.pNext                        = key.UsesDynamicRendering == VK_TRUE ? &rendering_info : nullptr;
        create_info.layout                       = job.PipelineLayout;
        create_info.renderPass                   = key.UsesDynamicRendering == VK_TRUE ? VK_NULL_HANDLE : job.RenderPass;
        create_info.subpass                      = key.UsesDynamicRendering == VK_TRUE ? 0 : key.Subpass;
        create_info.basePipelineIndex            = -1;
        return vkCreateGraphicsPipelines(device, worker_cache, 1, &create_info, nullptr, pipeline);
    }

    void PSOCache::PinPipeline(VkPipeline pipeline)
    {
        if (pipeline == VK_NULL_HANDLE)
            return;

        PipelinePinContext context = {};
        context.Handle             = pipeline;
        context.LastUsedFrame      = GetCurrentFrameMarker();
        context.Pin                = true;
        m_compute_pipelines.ForEach(&context, &AdjustComputePipelinePin);
        m_graphics_pipelines.ForEach(&context, &AdjustGraphicsPipelinePin);
    }

    void PSOCache::UnpinPipeline(VkPipeline pipeline)
    {
        if (pipeline == VK_NULL_HANDLE)
            return;

        PipelinePinContext context = {};
        context.Handle             = pipeline;
        m_compute_pipelines.ForEach(&context, &AdjustComputePipelinePin);
        m_graphics_pipelines.ForEach(&context, &AdjustGraphicsPipelinePin);
    }

    void PSOCache::EvictUnusedPipelines(uint64_t current_frame)
    {
        PipelineEvictionContext context = {};
        context.Cache                   = this;
        context.CurrentFrame            = current_frame;
        m_compute_pipelines.RemoveIf(&context, &RemoveEvictedComputePipeline);
        m_graphics_pipelines.RemoveIf(&context, &RemoveEvictedGraphicsPipeline);
    }

    bool PSOCache::LoadDriverPipelineCache(Core::VFS::IVFSContext& vfs)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr && m_driver_pipeline_cache != VK_NULL_HANDLE, "PSOCache is not initialized")
        ++m_telemetry.PersistentCacheLoadAttempts;

        const auto cache_path = Core::VFS::VFSPath::Parse(kPersistentCacheFile);
        if (cache_path.Failed())
            return false;

        const auto temp_path = Core::VFS::VFSPath::Parse(kPersistentCacheTempFile);
        if (temp_path.Succeeded())
            (void) vfs.Remove(temp_path.Value());

        auto opened = vfs.Open(cache_path.Value(), Core::VFS::VFSOpenFlags::Read);
        if (opened.Failed())
            return false;

        Core::VFS::IVFSFile* file = opened.Value();
        auto                 size = file->Size();
        if (size.Failed() || size.Value() < kPersistentCacheHeaderBytes || size.Value() > kPersistentCacheHeaderBytes + kMaxPersistentCacheBytes)
        {
            vfs.Close(file);
            return false;
        }

        uint8_t header[kPersistentCacheHeaderBytes] = {};
        if (!ReadFileExactly(*file, header, sizeof(header), 0) || !IsPersistentCacheHeaderValid(header, m_device->PhysicalDeviceProperties.properties, size.Value()))
        {
            vfs.Close(file);
            return false;
        }

        const uint64_t                   blob_size = ReadUint64LE(header + PersistentCacheBlobSizeOffset);
        auto                             scratch   = ZGetScratch(m_device->Arena);
        Core::Containers::Array<uint8_t> blob      = {};
        if (blob_size > 0)
        {
            blob.init(scratch.Arena, static_cast<size_t>(blob_size), static_cast<size_t>(blob_size));
            if (!ReadFileExactly(*file, blob.data(), blob.size(), kPersistentCacheHeaderBytes))
            {
                vfs.Close(file);
                ZReleaseScratch(scratch);
                return false;
            }
        }
        vfs.Close(file);

        if (CRC32(blob.data(), blob.size()) != ReadUint32LE(header + PersistentCacheCrc32Offset))
        {
            ZReleaseScratch(scratch);
            return false;
        }

        VkPipelineCacheCreateInfo create_info = {};
        create_info.sType                     = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        create_info.initialDataSize           = blob.size();
        create_info.pInitialData              = blob.data();
        VkPipelineCache restored_cache        = VK_NULL_HANDLE;
        const VkResult  result                = vkCreatePipelineCache(m_device->LogicalDevice, &create_info, nullptr, &restored_cache);
        ZReleaseScratch(scratch);
        if (result != VK_SUCCESS)
            return false;

        vkDestroyPipelineCache(m_device->LogicalDevice, m_driver_pipeline_cache, nullptr);
        m_driver_pipeline_cache = restored_cache;
        ++m_telemetry.PersistentCacheLoadSuccesses;
        m_telemetry.PersistentCacheLoadBytes += blob_size;
        return true;
    }

    bool PSOCache::SaveDriverPipelineCache(Core::VFS::IVFSContext& vfs)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr && m_driver_pipeline_cache != VK_NULL_HANDLE, "PSOCache is not initialized")
        ++m_telemetry.PersistentCacheSaveAttempts;

        size_t   blob_size = 0;
        VkResult result    = vkGetPipelineCacheData(m_device->LogicalDevice, m_driver_pipeline_cache, &blob_size, nullptr);
        if (result != VK_SUCCESS || blob_size > kMaxPersistentCacheBytes)
            return false;

        auto                             scratch = ZGetScratch(m_device->Arena);
        Core::Containers::Array<uint8_t> blob    = {};
        if (blob_size > 0)
        {
            blob.init(scratch.Arena, blob_size, blob_size);
            result = vkGetPipelineCacheData(m_device->LogicalDevice, m_driver_pipeline_cache, &blob_size, blob.data());
            if (result != VK_SUCCESS)
            {
                ZReleaseScratch(scratch);
                return false;
            }
        }

        const auto cache_directory = Core::VFS::VFSPath::Parse(kPersistentCacheDirectory);
        const auto cache_path      = Core::VFS::VFSPath::Parse(kPersistentCacheFile);
        const auto temp_path       = Core::VFS::VFSPath::Parse(kPersistentCacheTempFile);
        if (cache_directory.Failed() || cache_path.Failed() || temp_path.Failed() || vfs.CreateDir(cache_directory.Value()).Failed())
        {
            ZReleaseScratch(scratch);
            return false;
        }

        // A stale temporary cache never becomes authoritative; removal failure is harmless.
        (void) vfs.Remove(temp_path.Value());
        auto opened = vfs.Open(temp_path.Value(), Core::VFS::VFSOpenFlags::Write | Core::VFS::VFSOpenFlags::Create | Core::VFS::VFSOpenFlags::Truncate);
        if (opened.Failed())
        {
            ZReleaseScratch(scratch);
            return false;
        }

        uint8_t header[kPersistentCacheHeaderBytes] = {};
        MakePersistentCacheHeader(header, m_device->PhysicalDeviceProperties.properties, blob.size(), CRC32(blob.data(), blob.size()));
        Core::VFS::IVFSFile* file         = opened.Value();
        const bool           wrote_header = WriteFileExactly(*file, header, sizeof(header), 0);
        const bool           wrote_blob   = wrote_header && (blob.empty() || WriteFileExactly(*file, blob.data(), blob.size(), kPersistentCacheHeaderBytes));
        const bool           flushed      = wrote_blob && file->Flush().Succeeded();
        vfs.Close(file);
        ZReleaseScratch(scratch);
        if (!flushed)
        {
            (void) vfs.Remove(temp_path.Value());
            return false;
        }

        if (vfs.Rename(temp_path.Value(), cache_path.Value()).Failed())
        {
            (void) vfs.Remove(temp_path.Value());
            return false;
        }

        ++m_telemetry.PersistentCacheSaveSuccesses;
        m_telemetry.PersistentCacheSaveBytes += blob_size;
        return true;
    }

    void PSOCache::InvalidateShaderModules(const VkShaderModule* shader_modules, uint32_t module_count, uint32_t generation)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        if (!shader_modules || module_count == 0)
            return;

        ShaderInvalidationContext context = {};
        context.Cache                     = this;
        context.ShaderModules             = shader_modules;
        context.ModuleCount               = module_count;
        context.Generation                = generation;
        m_compute_pipelines.RemoveIf(&context, &RemoveInvalidatedComputePipeline);
        m_graphics_pipelines.RemoveIf(&context, &RemoveInvalidatedGraphicsPipeline);
        DispatchPipelineNotifications();
    }

    bool PSOCache::LoadWarmupRecipes(Core::VFS::IVFSContext& vfs)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        m_warmup_vfs = &vfs;
        ++m_telemetry.WarmupRecipeLoads;

        const auto recipe_path = Core::VFS::VFSPath::Parse(kWarmupRecipeFile);
        if (recipe_path.Failed())
            return false;

        const auto temp_path = Core::VFS::VFSPath::Parse(kWarmupRecipeTempFile);
        if (temp_path.Succeeded())
            (void) vfs.Remove(temp_path.Value());

        auto opened = vfs.Open(recipe_path.Value(), Core::VFS::VFSOpenFlags::Read);
        if (opened.Failed())
            return false;

        Core::VFS::IVFSFile* file                 = opened.Value();
        const auto           size                 = file->Size();
        constexpr uint64_t   max_recipe_file_size = kWarmupRecipeHeaderBytes + static_cast<uint64_t>(kMaxPSOWarmupRecipes) * sizeof(PSOWarmupRecipe);
        if (size.Failed() || size.Value() < kWarmupRecipeHeaderBytes || size.Value() > max_recipe_file_size)
        {
            vfs.Close(file);
            return false;
        }

        uint8_t header[kWarmupRecipeHeaderBytes] = {};
        if (!ReadFileExactly(*file, header, sizeof(header), 0) || !IsWarmupRecipeHeaderValid(header, size.Value()))
        {
            vfs.Close(file);
            return false;
        }

        const uint32_t                           recipe_count = ReadUint32LE(header + WarmupRecipeCountOffset);
        const uint64_t                           payload_size = ReadUint64LE(header + WarmupRecipePayloadBytesOffset);
        auto                                     scratch      = ZGetScratch(m_device->Arena);
        Core::Containers::Array<PSOWarmupRecipe> recipes      = {};
        if (recipe_count > 0)
        {
            recipes.init(scratch.Arena, recipe_count, recipe_count);
            if (!ReadFileExactly(*file, reinterpret_cast<uint8_t*>(recipes.data()), static_cast<size_t>(payload_size), kWarmupRecipeHeaderBytes))
            {
                vfs.Close(file);
                ZReleaseScratch(scratch);
                return false;
            }
        }
        vfs.Close(file);

        if (CRC32(reinterpret_cast<const uint8_t*>(recipes.data()), static_cast<size_t>(payload_size)) != ReadUint32LE(header + WarmupRecipeCrc32Offset))
        {
            ZReleaseScratch(scratch);
            return false;
        }

        m_warmup_recipe_count = 0;
        for (uint32_t index = 0; index < recipe_count; ++index)
        {
            const PSOWarmupRecipe& recipe = recipes[index];
            if (!IsWarmupRecipeValid(recipe))
            {
                ++m_telemetry.WarmupRecipeSkips;
                continue;
            }

            VkShaderStageFlags stages = 0;
            if (recipe.RecipeKind == PSOWarmupRecipe::Kind::Compute)
            {
                stages = VK_SHADER_STAGE_COMPUTE_BIT;
            }
            else
            {
                for (uint32_t stage_index = 0; stage_index < recipe.Graphics.ShaderStageCount; ++stage_index)
                    stages |= recipe.Graphics.ShaderStages[stage_index].Stage;
            }

            if (ComputeShaderContentHash(vfs, recipe.ShaderName, stages) != recipe.ShaderContentHash)
            {
                ++m_telemetry.WarmupRecipeSkips;
                continue;
            }

            const bool warmed = recipe.RecipeKind == PSOWarmupRecipe::Kind::Compute ? WarmupComputePipeline(recipe) : WarmupGraphicsPipeline(recipe);
            if (!warmed)
            {
                ++m_telemetry.WarmupRecipeSkips;
                continue;
            }
            RecordWarmupRecipe(recipe);
        }

        ZReleaseScratch(scratch);
        ++m_telemetry.WarmupRecipeLoadSuccesses;
        return true;
    }

    bool PSOCache::SaveWarmupRecipes(Core::VFS::IVFSContext& vfs)
    {
        ZENGINE_VALIDATE_ASSERT(m_device != nullptr, "PSOCache is not initialized")
        m_warmup_vfs = &vfs;
        ++m_telemetry.WarmupRecipeSaves;

        const auto cache_directory = Core::VFS::VFSPath::Parse(kPersistentCacheDirectory);
        const auto recipe_path     = Core::VFS::VFSPath::Parse(kWarmupRecipeFile);
        const auto temp_path       = Core::VFS::VFSPath::Parse(kWarmupRecipeTempFile);
        if (cache_directory.Failed() || recipe_path.Failed() || temp_path.Failed() || vfs.CreateDir(cache_directory.Value()).Failed())
            return false;

        // A temporary recipe file is never authoritative and can be discarded safely.
        (void) vfs.Remove(temp_path.Value());
        auto opened = vfs.Open(temp_path.Value(), Core::VFS::VFSOpenFlags::Write | Core::VFS::VFSOpenFlags::Create | Core::VFS::VFSOpenFlags::Truncate);
        if (opened.Failed())
            return false;

        const uint64_t payload_size                     = static_cast<uint64_t>(m_warmup_recipe_count) * sizeof(PSOWarmupRecipe);
        const auto*    payload                          = reinterpret_cast<const uint8_t*>(m_warmup_recipes);
        uint8_t        header[kWarmupRecipeHeaderBytes] = {};
        MakeWarmupRecipeHeader(header, m_warmup_recipe_count, payload_size, CRC32(payload, static_cast<size_t>(payload_size)));

        Core::VFS::IVFSFile* file          = opened.Value();
        const bool           wrote_header  = WriteFileExactly(*file, header, sizeof(header), 0);
        const bool           wrote_recipes = wrote_header && (payload_size == 0 || WriteFileExactly(*file, payload, static_cast<size_t>(payload_size), kWarmupRecipeHeaderBytes));
        const bool           flushed       = wrote_recipes && file->Flush().Succeeded();
        vfs.Close(file);
        if (!flushed)
        {
            (void) vfs.Remove(temp_path.Value());
            return false;
        }

        if (vfs.Rename(temp_path.Value(), recipe_path.Value()).Failed())
        {
            (void) vfs.Remove(temp_path.Value());
            return false;
        }

        ++m_telemetry.WarmupRecipeSaveSuccesses;
        return true;
    }

    uint64_t PSOCache::ComputeShaderContentHash(Core::VFS::IVFSContext& vfs, cstring shader_name, VkShaderStageFlags stages) const
    {
        if (!shader_name)
            return 0;

        constexpr uint64_t fnv_offset = 14695981039346656037ULL;
        constexpr uint64_t fnv_prime  = 1099511628211ULL;
        uint64_t           hash       = fnv_offset;
        const auto         hash_bytes = [&hash](const uint8_t* bytes, size_t byte_count) {
            for (size_t index = 0; index < byte_count; ++index)
            {
                hash ^= bytes[index];
                hash *= fnv_prime;
            }
        };
        const auto hash_stage = [&vfs, shader_name, &hash_bytes](cstring suffix, VkShaderStageFlagBits stage) {
            char      path[MAX_FILE_PATH_COUNT] = {};
            const int path_size                 = snprintf(path, sizeof(path), "/ZodiacEngine/Shaders/Cache/%s_%s.spv", shader_name, suffix);
            if (path_size <= 0 || static_cast<size_t>(path_size) >= sizeof(path))
                return false;

            const auto parsed_path = Core::VFS::VFSPath::Parse(path);
            if (parsed_path.Failed())
                return false;
            auto opened = vfs.Open(parsed_path.Value(), Core::VFS::VFSOpenFlags::Read);
            if (opened.Failed())
                return false;

            Core::VFS::IVFSFile* file = opened.Value();
            const auto           size = file->Size();
            if (size.Failed() || size.Value() == 0 || size.Value() > kMaxPersistentCacheBytes)
            {
                vfs.Close(file);
                return false;
            }

            uint8_t  bytes[4096] = {};
            uint64_t offset      = 0;
            while (offset < size.Value())
            {
                const size_t requested = static_cast<size_t>(std::min<uint64_t>(sizeof(bytes), size.Value() - offset));
                auto         read      = file->Read(Core::Containers::ArrayView<uint8_t>(bytes, requested), offset);
                if (read.Failed() || read.Value() != requested)
                {
                    vfs.Close(file);
                    return false;
                }
                hash_bytes(bytes, requested);
                offset += requested;
            }
            vfs.Close(file);

            const uint32_t stage_value = static_cast<uint32_t>(stage);
            hash_bytes(reinterpret_cast<const uint8_t*>(&stage_value), sizeof(stage_value));
            return true;
        };

        if ((stages & VK_SHADER_STAGE_VERTEX_BIT) != 0 && !hash_stage("vertex", VK_SHADER_STAGE_VERTEX_BIT))
            return 0;
        if ((stages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 && !hash_stage("fragment", VK_SHADER_STAGE_FRAGMENT_BIT))
            return 0;
        if ((stages & VK_SHADER_STAGE_COMPUTE_BIT) != 0 && !hash_stage("compute", VK_SHADER_STAGE_COMPUTE_BIT))
            return 0;
        return hash;
    }

    void PSOCache::RecordComputeWarmup(cstring shader_name, const PSOComputePipelineKey& key)
    {
        if (!m_warmup_vfs)
            return;

        PSOWarmupRecipe recipe               = {};
        recipe.RecipeKind                    = PSOWarmupRecipe::Kind::Compute;
        recipe.Compute                       = key;
        recipe.Compute.Shader.ModuleIdentity = 0;
        recipe.Compute.Shader.Generation     = 0;
        recipe.Compute.Layout                = {};
        if (!CopyWarmupShaderName(recipe.ShaderName, shader_name))
            return;
        recipe.ShaderContentHash = ComputeShaderContentHash(*m_warmup_vfs, recipe.ShaderName, VK_SHADER_STAGE_COMPUTE_BIT);
        if (recipe.ShaderContentHash == 0)
            return;
        RecordWarmupRecipe(recipe);
    }

    void PSOCache::RecordGraphicsWarmup(cstring shader_name, const PSOGraphicsPipelineKey& key, const PSOCompatibilityRenderPassKey& compatibility)
    {
        if (!m_warmup_vfs)
            return;

        PSOWarmupRecipe recipe     = {};
        recipe.RecipeKind          = PSOWarmupRecipe::Kind::Graphics;
        recipe.Graphics            = key;
        recipe.Compatibility       = compatibility;
        recipe.Graphics.Layout     = {};
        recipe.Graphics.RenderPass = {};
        VkShaderStageFlags stages  = 0;
        for (uint32_t stage_index = 0; stage_index < recipe.Graphics.ShaderStageCount; ++stage_index)
        {
            stages                                                   |= static_cast<VkShaderStageFlags>(recipe.Graphics.ShaderStages[stage_index].Stage);
            recipe.Graphics.ShaderStages[stage_index].ModuleIdentity  = 0;
            recipe.Graphics.ShaderStages[stage_index].Generation      = 0;
        }
        if (!CopyWarmupShaderName(recipe.ShaderName, shader_name))
            return;
        recipe.ShaderContentHash = ComputeShaderContentHash(*m_warmup_vfs, recipe.ShaderName, stages);
        if (recipe.ShaderContentHash == 0)
            return;
        RecordWarmupRecipe(recipe);
    }

    void PSOCache::RecordWarmupRecipe(const PSOWarmupRecipe& recipe)
    {
        for (uint32_t index = 0; index < m_warmup_recipe_count; ++index)
        {
            const PSOWarmupRecipe& existing = m_warmup_recipes[index];
            if (existing.RecipeKind != recipe.RecipeKind || existing.ShaderContentHash != recipe.ShaderContentHash || Helpers::secure_strcmp(existing.ShaderName, recipe.ShaderName) != 0)
                continue;
            if ((recipe.RecipeKind == PSOWarmupRecipe::Kind::Compute && existing.Compute == recipe.Compute) || (recipe.RecipeKind == PSOWarmupRecipe::Kind::Graphics && existing.Graphics == recipe.Graphics && existing.Compatibility == recipe.Compatibility))
                return;
        }

        if (m_warmup_recipe_count == kMaxPSOWarmupRecipes)
        {
            ++m_telemetry.WarmupRecipeSkips;
            return;
        }
        m_warmup_recipes[m_warmup_recipe_count++] = recipe;
    }

    bool PSOCache::WarmupComputePipeline(const PSOWarmupRecipe& recipe)
    {
        Shaders::Shader* shader = CompileWarmupShader(m_device, recipe.ShaderName);
        if (!shader)
            return false;

        const VkPipelineShaderStageCreateInfo* stage = FindShaderStage(*shader, VK_SHADER_STAGE_COMPUTE_BIT);
        if (!stage)
            return false;

        VkPipelineLayoutCreateInfo layout_create_info                                                                = {};
        layout_create_info.sType                                                                                     = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_create_info.setLayoutCount                                                                            = static_cast<uint32_t>(shader->SetLayouts.size());
        layout_create_info.pSetLayouts                                                                               = shader->SetLayouts.data();
        layout_create_info.pushConstantRangeCount                                                                    = static_cast<uint32_t>(shader->PushConstants.size());
        layout_create_info.pPushConstantRanges                                                                       = shader->PushConstants.data();
        const VkPipelineLayout      layout                                                                           = GetOrCreatePipelineLayout(layout_create_info);

        VkComputePipelineCreateInfo create_info                                                                      = {};
        create_info.sType                                                                                            = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        create_info.flags                                                                                            = recipe.Compute.CreateFlags;
        create_info.stage                                                                                            = *stage;
        VkSpecializationMapEntry specialization_entries[kMaxPSOSpecializationEntries]                                = {};
        uint8_t                  specialization_data[kMaxPSOSpecializationEntries * kMaxPSOSpecializationValueBytes] = {};
        VkSpecializationInfo     specialization_info                                                                 = {};
        BuildSpecializationInfo(recipe.Compute.Shader.Specialization, specialization_entries, specialization_data, &specialization_info);
        create_info.stage.pSpecializationInfo = recipe.Compute.Shader.Specialization.EntryCount == 0 ? nullptr : &specialization_info;
        create_info.layout                    = layout;
        create_info.basePipelineIndex         = -1;
        // Boot warmup never needs the pipeline on this call site. Queue it so normal
        // frame creation remains synchronous only when a pipeline is actually needed.
        (void) RequestComputePipelineAsync(create_info, shader->Generation, nullptr, nullptr, true);
        return true;
    }

    bool PSOCache::WarmupGraphicsPipeline(const PSOWarmupRecipe& recipe)
    {
        Shaders::Shader* shader = CompileWarmupShader(m_device, recipe.ShaderName);
        if (!shader)
            return false;

        const PSOGraphicsPipelineKey&   key                                                                                                              = recipe.Graphics;
        VkPipelineShaderStageCreateInfo stages[kMaxPSOGraphicsShaderStages]                                                                              = {};
        VkSpecializationMapEntry        specialization_entries[kMaxPSOGraphicsShaderStages][kMaxPSOSpecializationEntries]                                = {};
        uint8_t                         specialization_data[kMaxPSOGraphicsShaderStages][kMaxPSOSpecializationEntries * kMaxPSOSpecializationValueBytes] = {};
        VkSpecializationInfo            specialization_infos[kMaxPSOGraphicsShaderStages]                                                                = {};
        for (uint32_t index = 0; index < key.ShaderStageCount; ++index)
        {
            const VkPipelineShaderStageCreateInfo* stage = FindShaderStage(*shader, static_cast<VkShaderStageFlagBits>(key.ShaderStages[index].Stage));
            if (!stage)
                return false;
            stages[index] = *stage;
            BuildSpecializationInfo(key.ShaderStages[index].Specialization, specialization_entries[index], specialization_data[index], &specialization_infos[index]);
            stages[index].pSpecializationInfo = key.ShaderStages[index].Specialization.EntryCount == 0 ? nullptr : &specialization_infos[index];
        }

        VkVertexInputBindingDescription vertex_bindings[kMaxPSOVertexBindings] = {};
        for (uint32_t index = 0; index < key.VertexBindingCount; ++index)
        {
            vertex_bindings[index] = {
                .binding   = key.VertexBindings[index].Binding,
                .stride    = key.VertexBindings[index].Stride,
                .inputRate = static_cast<VkVertexInputRate>(key.VertexBindings[index].InputRate),
            };
        }
        VkVertexInputAttributeDescription vertex_attributes[kMaxPSOVertexAttributes] = {};
        for (uint32_t index = 0; index < key.VertexAttributeCount; ++index)
        {
            vertex_attributes[index] = {
                .location = key.VertexAttributes[index].Location,
                .binding  = key.VertexAttributes[index].Binding,
                .format   = static_cast<VkFormat>(key.VertexAttributes[index].Format),
                .offset   = key.VertexAttributes[index].Offset,
            };
        }

        VkPipelineVertexInputStateCreateInfo vertex_input                                         = {};
        vertex_input.sType                                                                        = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount                                                = key.VertexBindingCount;
        vertex_input.pVertexBindingDescriptions                                                   = key.VertexBindingCount == 0 ? nullptr : vertex_bindings;
        vertex_input.vertexAttributeDescriptionCount                                              = key.VertexAttributeCount;
        vertex_input.pVertexAttributeDescriptions                                                 = key.VertexAttributeCount == 0 ? nullptr : vertex_attributes;

        VkPipelineInputAssemblyStateCreateInfo input_assembly                                     = {};
        input_assembly.sType                                                                      = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology                                                                   = static_cast<VkPrimitiveTopology>(key.InputAssemblyTopology);
        input_assembly.primitiveRestartEnable                                                     = key.PrimitiveRestartEnable;

        VkPipelineViewportStateCreateInfo viewport                                                = {};
        viewport.sType                                                                            = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount                                                                    = key.ViewportCount;
        viewport.scissorCount                                                                     = key.ScissorCount;

        VkPipelineRasterizationStateCreateInfo rasterization                                      = {};
        rasterization.sType                                                                       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.flags                                                                       = key.RasterizationFlags;
        rasterization.depthClampEnable                                                            = key.DepthClampEnable;
        rasterization.rasterizerDiscardEnable                                                     = key.RasterizerDiscardEnable;
        rasterization.polygonMode                                                                 = static_cast<VkPolygonMode>(key.PolygonMode);
        rasterization.cullMode                                                                    = key.CullMode;
        rasterization.frontFace                                                                   = static_cast<VkFrontFace>(key.FrontFace);
        rasterization.depthBiasEnable                                                             = key.DepthBiasEnable;
        rasterization.depthBiasConstantFactor                                                     = std::bit_cast<float>(key.DepthBiasConstantFactorBits);
        rasterization.depthBiasClamp                                                              = std::bit_cast<float>(key.DepthBiasClampBits);
        rasterization.depthBiasSlopeFactor                                                        = std::bit_cast<float>(key.DepthBiasSlopeFactorBits);
        rasterization.lineWidth                                                                   = std::bit_cast<float>(key.LineWidthBits);

        VkPipelineMultisampleStateCreateInfo multisample                                          = {};
        multisample.sType                                                                         = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.flags                                                                         = key.MultisampleFlags;
        multisample.rasterizationSamples                                                          = static_cast<VkSampleCountFlagBits>(key.RasterizationSamples);
        multisample.sampleShadingEnable                                                           = key.SampleShadingEnable;
        multisample.minSampleShading                                                              = std::bit_cast<float>(key.MinSampleShadingBits);
        multisample.alphaToCoverageEnable                                                         = key.AlphaToCoverageEnable;
        multisample.alphaToOneEnable                                                              = key.AlphaToOneEnable;

        VkPipelineDepthStencilStateCreateInfo depth_stencil                                       = {};
        depth_stencil.sType                                                                       = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.flags                                                                       = key.DepthStencilFlags;
        depth_stencil.depthTestEnable                                                             = key.DepthTestEnable;
        depth_stencil.depthWriteEnable                                                            = key.DepthWriteEnable;
        depth_stencil.depthCompareOp                                                              = static_cast<VkCompareOp>(key.DepthCompareOp);
        depth_stencil.depthBoundsTestEnable                                                       = key.DepthBoundsTestEnable;
        depth_stencil.stencilTestEnable                                                           = key.StencilTestEnable;
        depth_stencil.front                                                                       = MakeVulkanStencilState(key.FrontStencil);
        depth_stencil.back                                                                        = MakeVulkanStencilState(key.BackStencil);
        depth_stencil.minDepthBounds                                                              = std::bit_cast<float>(key.MinDepthBoundsBits);
        depth_stencil.maxDepthBounds                                                              = std::bit_cast<float>(key.MaxDepthBoundsBits);

        VkPipelineColorBlendAttachmentState color_blend_attachments[kMaxPSOColorBlendAttachments] = {};
        for (uint32_t index = 0; index < key.ColorBlendAttachmentCount; ++index)
        {
            const PSOColorBlendAttachmentKey& source = key.ColorBlendAttachments[index];
            color_blend_attachments[index]           = {
                .blendEnable         = source.BlendEnable,
                .srcColorBlendFactor = static_cast<VkBlendFactor>(source.SrcColorBlendFactor),
                .dstColorBlendFactor = static_cast<VkBlendFactor>(source.DstColorBlendFactor),
                .colorBlendOp        = static_cast<VkBlendOp>(source.ColorBlendOp),
                .srcAlphaBlendFactor = static_cast<VkBlendFactor>(source.SrcAlphaBlendFactor),
                .dstAlphaBlendFactor = static_cast<VkBlendFactor>(source.DstAlphaBlendFactor),
                .alphaBlendOp        = static_cast<VkBlendOp>(source.AlphaBlendOp),
                .colorWriteMask      = source.ColorWriteMask,
            };
        }
        VkPipelineColorBlendAttachmentState dummy_attachment = {};
        VkPipelineColorBlendStateCreateInfo color_blend      = {};
        color_blend.sType                                    = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.flags                                    = key.ColorBlendFlags;
        color_blend.logicOpEnable                            = key.LogicOpEnable;
        color_blend.logicOp                                  = static_cast<VkLogicOp>(key.LogicOp);
        color_blend.attachmentCount                          = key.ColorBlendAttachmentCount;
        color_blend.pAttachments                             = key.ColorBlendAttachmentCount == 0 ? &dummy_attachment : color_blend_attachments;
        for (uint32_t index = 0; index < 4; ++index)
            color_blend.blendConstants[index] = std::bit_cast<float>(key.BlendConstantBits[index]);

        VkDynamicState                   dynamic_states[]                                   = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state                                      = {};
        dynamic_state.sType                                                                 = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount                                                     = static_cast<uint32_t>(sizeof(dynamic_states) / sizeof(dynamic_states[0]));
        dynamic_state.pDynamicStates                                                        = dynamic_states;

        VkPipelineRenderingCreateInfo rendering_info                                        = {};
        VkFormat                      rendering_color_formats[kMaxPSOColorBlendAttachments] = {};
        if (key.UsesDynamicRendering == VK_TRUE)
        {
            for (uint32_t index = 0; index < key.RenderingColorAttachmentCount; ++index)
                rendering_color_formats[index] = static_cast<VkFormat>(key.RenderingColorAttachmentFormats[index]);
            rendering_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering_info.viewMask                = key.RenderingViewMask;
            rendering_info.colorAttachmentCount    = key.RenderingColorAttachmentCount;
            rendering_info.pColorAttachmentFormats = key.RenderingColorAttachmentCount == 0 ? nullptr : rendering_color_formats;
            rendering_info.depthAttachmentFormat   = static_cast<VkFormat>(key.RenderingDepthAttachmentFormat);
            rendering_info.stencilAttachmentFormat = static_cast<VkFormat>(key.RenderingStencilAttachmentFormat);
        }

        VkPipelineLayoutCreateInfo layout_create_info          = {};
        layout_create_info.sType                               = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_create_info.setLayoutCount                      = static_cast<uint32_t>(shader->SetLayouts.size());
        layout_create_info.pSetLayouts                         = shader->SetLayouts.data();
        layout_create_info.pushConstantRangeCount              = static_cast<uint32_t>(shader->PushConstants.size());
        layout_create_info.pPushConstantRanges                 = shader->PushConstants.data();
        const VkPipelineLayout       layout                    = GetOrCreatePipelineLayout(layout_create_info);
        const VkRenderPass           compatibility_render_pass = key.UsesDynamicRendering == VK_TRUE ? VK_NULL_HANDLE : GetOrCreateCompatibilityRenderPass(recipe.Compatibility);

        VkGraphicsPipelineCreateInfo create_info               = {};
        create_info.sType                                      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        create_info.flags                                      = key.CreateFlags;
        create_info.stageCount                                 = key.ShaderStageCount;
        create_info.pStages                                    = stages;
        create_info.pVertexInputState                          = &vertex_input;
        create_info.pInputAssemblyState                        = &input_assembly;
        create_info.pViewportState                             = &viewport;
        create_info.pRasterizationState                        = &rasterization;
        create_info.pMultisampleState                          = &multisample;
        create_info.pDepthStencilState                         = key.HasDepthStencilState == VK_TRUE ? &depth_stencil : nullptr;
        create_info.pColorBlendState                           = &color_blend;
        create_info.pDynamicState                              = &dynamic_state;
        create_info.pNext                                      = key.UsesDynamicRendering == VK_TRUE ? &rendering_info : nullptr;
        create_info.layout                                     = layout;
        create_info.renderPass                                 = compatibility_render_pass;
        create_info.subpass                                    = key.UsesDynamicRendering == VK_TRUE ? 0 : key.Subpass;
        create_info.basePipelineIndex                          = -1;
        // Boot warmup has no immediate consumer, so publication can occur next frame.
        (void) RequestGraphicsPipelineAsync(create_info, shader->Generation, nullptr, nullptr, true);
        return true;
    }

    const PSOCacheTelemetry& PSOCache::GetTelemetry() const
    {
        return m_telemetry;
    }

    void PSOCache::LogTelemetry() const {ZENGINE_LOG_RENDER_INFO(
        "PSO cache: samplers {}/{}; descriptor layouts {}/{}; pipeline layouts {}/{}; compatibility passes {}/{}; compute pipelines {}/{}; graphics pipelines {}/{}; disk load {}/{} ({} B); disk save {}/{} ({} B)",
        m_telemetry.SamplerHits,
        m_telemetry.SamplerMisses,
        m_telemetry.DescriptorSetLayoutHits,
        m_telemetry.DescriptorSetLayoutMisses,
        m_telemetry.PipelineLayoutHits,
        m_telemetry.PipelineLayoutMisses,
        m_telemetry.CompatibilityRenderPassHits,
        m_telemetry.CompatibilityRenderPassMisses,
        m_telemetry.ComputePipelineHits,
        m_telemetry.ComputePipelineMisses,
        m_telemetry.GraphicsPipelineHits,
        m_telemetry.GraphicsPipelineMisses,
        m_telemetry.PersistentCacheLoadSuccesses,
        m_telemetry.PersistentCacheLoadAttempts,
        m_telemetry.PersistentCacheLoadBytes,
        m_telemetry.PersistentCacheSaveSuccesses,
        m_telemetry.PersistentCacheSaveAttempts,
        m_telemetry.PersistentCacheSaveBytes)}

    VkPipelineCache PSOCache::GetDriverPipelineCache() const
    {
        ZENGINE_VALIDATE_ASSERT(m_driver_pipeline_cache != VK_NULL_HANDLE, "PSOCache driver cache is not initialized")
        return m_driver_pipeline_cache;
    }

    PSODescriptorSetLayoutKey PSOCache::MakeDescriptorSetLayoutKey(const VkDescriptorSetLayoutCreateInfo& create_info)
    {
        ZENGINE_VALIDATE_ASSERT(create_info.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, "Invalid descriptor-set layout create info")
        ZENGINE_VALIDATE_ASSERT((create_info.flags & ~kSupportedDescriptorSetLayoutFlags) == 0, "Unsupported descriptor-set layout create flags in PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.bindingCount <= kMaxPSOLayoutBindings, "PSO descriptor-set layout binding capacity exceeded")
        ZENGINE_VALIDATE_ASSERT(create_info.bindingCount == 0 || create_info.pBindings != nullptr, "Descriptor-set layout bindings are null")

        const auto* binding_flags_info = GetBindingFlagsCreateInfo(create_info);
        ZENGINE_VALIDATE_ASSERT(binding_flags_info == nullptr || binding_flags_info->pBindingFlags != nullptr || create_info.bindingCount == 0, "Descriptor binding flags are null")
        PSODescriptorSetLayoutKey key = {};
        key.CreateFlags               = create_info.flags;
        key.BindingCount              = create_info.bindingCount;
        for (uint32_t i = 0; i < create_info.bindingCount; ++i)
        {
            const auto& binding = create_info.pBindings[i];
            ZENGINE_VALIDATE_ASSERT(binding.descriptorCount != 0, "Descriptor-set layout binding count must be non-zero")
            ValidateShaderStages(binding.stageFlags);

            PSODescriptorSetLayoutBindingKey& destination = key.Bindings[i];
            destination.Binding                           = binding.binding;
            destination.DescriptorCount                   = binding.descriptorCount;
            destination.ShaderStages                      = binding.stageFlags;
            destination.BindingFlags                      = binding_flags_info ? binding_flags_info->pBindingFlags[i] : 0;
            if (binding.pImmutableSamplers)
            {
                ZENGINE_VALIDATE_ASSERT(binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER || binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "Immutable samplers require sampler descriptor types")
                destination.ImmutableSamplerListIdentity = GetOrCreateImmutableSamplerList(binding.pImmutableSamplers, binding.descriptorCount);
            }
            destination.DescriptorKind = ToPSODescriptorKind(binding.descriptorType);
            ZENGINE_VALIDATE_ASSERT((destination.BindingFlags & ~kSupportedDescriptorBindingFlags) == 0, "Unsupported descriptor binding flags in PSO cache")
        }
        SortDescriptorBindings(key);
        return key;
    }

    PSOSamplerKey PSOCache::MakeSamplerKey(const VkSamplerCreateInfo& create_info)
    {
        ZENGINE_VALIDATE_ASSERT(create_info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, "Invalid sampler create info")
        ZENGINE_VALIDATE_ASSERT(create_info.pNext == nullptr, "Sampler pNext chains are not supported by PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.flags == 0, "Unsupported sampler create flags in PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.anisotropyEnable == VK_FALSE || create_info.anisotropyEnable == VK_TRUE, "Invalid sampler anisotropy flag")
        ZENGINE_VALIDATE_ASSERT(create_info.compareEnable == VK_FALSE || create_info.compareEnable == VK_TRUE, "Invalid sampler compare flag")
        ZENGINE_VALIDATE_ASSERT(create_info.unnormalizedCoordinates == VK_FALSE || create_info.unnormalizedCoordinates == VK_TRUE, "Invalid sampler coordinate flag")

        PSOSamplerKey key      = {};
        key.CreateFlags        = create_info.flags;
        key.MipLodBiasBits     = std::bit_cast<uint32_t>(create_info.mipLodBias);
        key.MaxAnisotropyBits  = std::bit_cast<uint32_t>(create_info.maxAnisotropy);
        key.MinLodBits         = std::bit_cast<uint32_t>(create_info.minLod);
        key.MaxLodBits         = std::bit_cast<uint32_t>(create_info.maxLod);
        key.CompareOp          = ToPSOCompareOp(create_info.compareOp);
        key.BorderColor        = ToPSOBorderColor(create_info.borderColor);
        key.MagFilter          = ToPSOFilter(create_info.magFilter);
        key.MinFilter          = ToPSOFilter(create_info.minFilter);
        key.MipmapMode         = ToPSOMipmapMode(create_info.mipmapMode);
        key.AddressModeU       = ToPSOAddressMode(create_info.addressModeU);
        key.AddressModeV       = ToPSOAddressMode(create_info.addressModeV);
        key.AddressModeW       = ToPSOAddressMode(create_info.addressModeW);
        key.AnisotropyEnabled  = create_info.anisotropyEnable == VK_TRUE;
        key.CompareEnabled     = create_info.compareEnable == VK_TRUE;
        key.UnnormalizedCoords = create_info.unnormalizedCoordinates == VK_TRUE;
        return key;
    }

    PSOPipelineLayoutKey PSOCache::MakePipelineLayoutKey(const VkPipelineLayoutCreateInfo& create_info) const
    {
        ZENGINE_VALIDATE_ASSERT(create_info.sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, "Invalid pipeline-layout create info")
        ZENGINE_VALIDATE_ASSERT(create_info.pNext == nullptr, "Pipeline-layout pNext chains are not supported by PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.flags == 0, "Unsupported pipeline-layout create flags in PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.setLayoutCount <= kMaxPSOSetLayouts, "PSO pipeline-layout set-layout capacity exceeded")
        ZENGINE_VALIDATE_ASSERT(create_info.pushConstantRangeCount <= kMaxPSOPushConstantRanges, "PSO pipeline-layout push-constant capacity exceeded")
        ZENGINE_VALIDATE_ASSERT(create_info.setLayoutCount == 0 || create_info.pSetLayouts != nullptr, "Pipeline-layout set layouts are null")
        ZENGINE_VALIDATE_ASSERT(create_info.pushConstantRangeCount == 0 || create_info.pPushConstantRanges != nullptr, "Pipeline-layout push constants are null")

        PSOPipelineLayoutKey key = {};
        key.SetLayoutCount       = create_info.setLayoutCount;
        key.PushConstantCount    = create_info.pushConstantRangeCount;
        for (uint32_t i = 0; i < key.SetLayoutCount; ++i)
        {
            const uint64_t cached_identity   = GetSetLayoutIdentity(create_info.pSetLayouts[i]);
            key.SetLayouts[i].CachedIdentity = cached_identity;
            if (cached_identity == 0)
                key.SetLayouts[i].ExternalHandle = create_info.pSetLayouts[i];
        }
        for (uint32_t i = 0; i < key.PushConstantCount; ++i)
        {
            const auto& range = create_info.pPushConstantRanges[i];
            ValidateShaderStages(range.stageFlags);
            ZENGINE_VALIDATE_ASSERT(range.size != 0, "Pipeline-layout push-constant size must be non-zero")
            key.PushConstants[i] = {.Offset = range.offset, .Size = range.size, .ShaderStages = range.stageFlags};
        }
        SortPushConstantRanges(key);
        return key;
    }

    PSOCompatibilityRenderPassKey PSOCache::MakeCompatibilityRenderPassKey(const RenderPasses::Attachment& attachment) const
    {
        const auto& specification = attachment.GetSpecification();
        ZENGINE_VALIDATE_ASSERT(specification.ColorsMap.size() <= kMaxPSOCompatibilityAttachments, "PSO compatibility render-pass attachment capacity exceeded")

        PSOCompatibilityRenderPassKey key = {};
        key.AttachmentCount               = static_cast<uint32_t>(specification.ColorsMap.size());
        for (uint32_t i = 0; i < key.AttachmentCount; ++i)
        {
            const auto& source          = specification.ColorsMap.at(i);
            auto&       destination     = key.Attachments[i];
            destination.Format          = ResolveAttachmentFormat(m_device, source.Format);
            destination.Samples         = VK_SAMPLE_COUNT_1_BIT;
            destination.ReferenceLayout = ResolveReferenceLayout(source.ReferenceLayout);
            if (destination.ReferenceLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                ZENGINE_VALIDATE_ASSERT(key.DepthAttachmentIndex == VK_ATTACHMENT_UNUSED, "PSO compatibility render passes support one depth attachment")
                key.DepthAttachmentIndex = i;
            }
            else
            {
                ++key.ColorAttachmentCount;
            }
        }
        return key;
    }

    PSOComputePipelineKey PSOCache::MakeComputePipelineKey(const VkComputePipelineCreateInfo& create_info, uint32_t shader_generation) const
    {
        ZENGINE_VALIDATE_ASSERT(create_info.sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, "Invalid compute pipeline create info")
        ZENGINE_VALIDATE_ASSERT(create_info.pNext == nullptr, "Compute pipeline pNext chains are not supported by PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.flags == 0, "Unsupported compute pipeline flags in PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.basePipelineHandle == VK_NULL_HANDLE && create_info.basePipelineIndex == -1, "Pipeline derivatives are not supported by PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.layout != VK_NULL_HANDLE, "Compute pipeline layout is null")
        ZENGINE_VALIDATE_ASSERT(create_info.stage.sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, "Invalid compute shader stage create info")
        ZENGINE_VALIDATE_ASSERT(create_info.stage.pNext == nullptr && create_info.stage.flags == 0, "Unsupported compute shader stage create info")
        ZENGINE_VALIDATE_ASSERT(create_info.stage.stage == VK_SHADER_STAGE_COMPUTE_BIT, "Compute pipeline stage is not compute")
        ZENGINE_VALIDATE_ASSERT(create_info.stage.module != VK_NULL_HANDLE, "Compute pipeline shader module is null")
        ZENGINE_VALIDATE_ASSERT(create_info.stage.pName != nullptr && Helpers::secure_strcmp(create_info.stage.pName, "main") == 0, "Only the main compute entry point is supported by PSO cache")

        PSOComputePipelineKey key      = {};
        key.CreateFlags                = create_info.flags;
        key.Shader.ModuleIdentity      = reinterpret_cast<uintptr_t>(create_info.stage.module);
        key.Shader.Generation          = shader_generation;
        key.Shader.Specialization      = MakeSpecializationKey(create_info.stage.pSpecializationInfo);
        const uint64_t layout_identity = GetPipelineLayoutIdentity(create_info.layout);
        key.Layout.CachedIdentity      = layout_identity;
        if (layout_identity == 0)
            key.Layout.ExternalHandle = create_info.layout;
        return key;
    }

    PSOGraphicsPipelineKey PSOCache::MakeGraphicsPipelineKey(const VkGraphicsPipelineCreateInfo& create_info, uint32_t shader_generation) const
    {
        ZENGINE_VALIDATE_ASSERT(create_info.sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, "Invalid graphics pipeline create info")
        ZENGINE_VALIDATE_ASSERT(create_info.flags == 0, "Unsupported graphics pipeline flags in PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.stageCount != 0 && create_info.stageCount <= kMaxPSOGraphicsShaderStages && create_info.pStages != nullptr, "PSO graphics shader-stage capacity exceeded")
        ZENGINE_VALIDATE_ASSERT(create_info.layout != VK_NULL_HANDLE, "Graphics pipeline layout is null")
        ZENGINE_VALIDATE_ASSERT(create_info.pVertexInputState != nullptr && create_info.pInputAssemblyState != nullptr && create_info.pViewportState != nullptr && create_info.pRasterizationState != nullptr && create_info.pMultisampleState != nullptr && create_info.pColorBlendState != nullptr && create_info.pDynamicState != nullptr, "Graphics pipeline fixed state is null")
        ZENGINE_VALIDATE_ASSERT(create_info.pTessellationState == nullptr, "Tessellation state is not supported by the synchronous PSO cache")
        ZENGINE_VALIDATE_ASSERT(create_info.basePipelineHandle == VK_NULL_HANDLE && create_info.basePipelineIndex == -1, "Pipeline derivatives are not supported by PSO cache")

        const VkPipelineRenderingCreateInfo* rendering_info = nullptr;
        if (create_info.pNext != nullptr)
        {
            rendering_info = static_cast<const VkPipelineRenderingCreateInfo*>(create_info.pNext);
            ZENGINE_VALIDATE_ASSERT(rendering_info->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO && rendering_info->pNext == nullptr, "Unsupported graphics pipeline pNext chain")
            ZENGINE_VALIDATE_ASSERT(create_info.renderPass == VK_NULL_HANDLE && create_info.subpass == 0, "Dynamic-rendering graphics pipelines cannot specify a render pass or subpass")
            ZENGINE_VALIDATE_ASSERT(rendering_info->colorAttachmentCount <= kMaxPSOColorBlendAttachments, "PSO dynamic-rendering color-format capacity exceeded")
            ZENGINE_VALIDATE_ASSERT(rendering_info->colorAttachmentCount == 0 || rendering_info->pColorAttachmentFormats != nullptr, "Dynamic-rendering color formats are null")
        }
        else
        {
            ZENGINE_VALIDATE_ASSERT(create_info.renderPass != VK_NULL_HANDLE, "Graphics pipeline render pass is null")
        }

        const auto& vertex_input   = *create_info.pVertexInputState;
        const auto& input_assembly = *create_info.pInputAssemblyState;
        const auto& viewport       = *create_info.pViewportState;
        const auto& rasterization  = *create_info.pRasterizationState;
        const auto& multisample    = *create_info.pMultisampleState;
        const auto& color_blend    = *create_info.pColorBlendState;
        const auto& dynamic_state  = *create_info.pDynamicState;

        ZENGINE_VALIDATE_ASSERT(vertex_input.sType == VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO && vertex_input.pNext == nullptr && vertex_input.flags == 0, "Unsupported graphics vertex-input state")
        ZENGINE_VALIDATE_ASSERT(vertex_input.vertexBindingDescriptionCount <= kMaxPSOVertexBindings && vertex_input.vertexAttributeDescriptionCount <= kMaxPSOVertexAttributes, "PSO graphics vertex-input capacity exceeded")
        ZENGINE_VALIDATE_ASSERT(vertex_input.vertexBindingDescriptionCount == 0 || vertex_input.pVertexBindingDescriptions != nullptr, "Graphics pipeline vertex bindings are null")
        ZENGINE_VALIDATE_ASSERT(vertex_input.vertexAttributeDescriptionCount == 0 || vertex_input.pVertexAttributeDescriptions != nullptr, "Graphics pipeline vertex attributes are null")
        ZENGINE_VALIDATE_ASSERT(input_assembly.sType == VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO && input_assembly.pNext == nullptr && input_assembly.flags == 0, "Unsupported graphics input-assembly state")
        ZENGINE_VALIDATE_ASSERT(viewport.sType == VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO && viewport.pNext == nullptr && viewport.flags == 0, "Unsupported graphics viewport state")
        ZENGINE_VALIDATE_ASSERT(viewport.viewportCount == 1 && viewport.scissorCount == 1 && viewport.pViewports == nullptr && viewport.pScissors == nullptr, "PSO cache requires dynamic viewport and scissor state")
        ZENGINE_VALIDATE_ASSERT(rasterization.sType == VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO && rasterization.pNext == nullptr, "Unsupported graphics rasterization state")
        ZENGINE_VALIDATE_ASSERT(multisample.sType == VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO && multisample.pNext == nullptr && multisample.pSampleMask == nullptr, "Unsupported graphics multisample state")
        ZENGINE_VALIDATE_ASSERT(color_blend.sType == VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO && color_blend.pNext == nullptr, "Unsupported graphics color-blend state")
        ZENGINE_VALIDATE_ASSERT(color_blend.attachmentCount <= kMaxPSOColorBlendAttachments, "PSO graphics color-blend attachment capacity exceeded")
        ZENGINE_VALIDATE_ASSERT(color_blend.attachmentCount == 0 || color_blend.pAttachments != nullptr, "Graphics pipeline color-blend attachments are null")
        ZENGINE_VALIDATE_ASSERT(rendering_info == nullptr || color_blend.attachmentCount == rendering_info->colorAttachmentCount, "Dynamic-rendering pipeline color-blend state does not match attachment formats")
        ZENGINE_VALIDATE_ASSERT(dynamic_state.sType == VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO && dynamic_state.pNext == nullptr && dynamic_state.flags == 0, "Unsupported graphics dynamic state")
        ZENGINE_VALIDATE_ASSERT(dynamic_state.dynamicStateCount == 2 && dynamic_state.pDynamicStates != nullptr, "PSO cache requires exactly dynamic viewport and scissor state")

        bool has_viewport = false;
        bool has_scissor  = false;
        for (uint32_t i = 0; i < dynamic_state.dynamicStateCount; ++i)
        {
            has_viewport |= dynamic_state.pDynamicStates[i] == VK_DYNAMIC_STATE_VIEWPORT;
            has_scissor  |= dynamic_state.pDynamicStates[i] == VK_DYNAMIC_STATE_SCISSOR;
        }
        ZENGINE_VALIDATE_ASSERT(has_viewport && has_scissor, "PSO cache requires dynamic viewport and scissor state")

        PSOGraphicsPipelineKey key      = {};
        key.CreateFlags                 = create_info.flags;
        key.ShaderStageCount            = create_info.stageCount;
        key.Subpass                     = create_info.subpass;
        key.VertexBindingCount          = vertex_input.vertexBindingDescriptionCount;
        key.VertexAttributeCount        = vertex_input.vertexAttributeDescriptionCount;
        key.InputAssemblyTopology       = static_cast<uint32_t>(input_assembly.topology);
        key.PrimitiveRestartEnable      = input_assembly.primitiveRestartEnable;
        key.ViewportCount               = viewport.viewportCount;
        key.ScissorCount                = viewport.scissorCount;
        key.RasterizationFlags          = rasterization.flags;
        key.DepthClampEnable            = rasterization.depthClampEnable;
        key.RasterizerDiscardEnable     = rasterization.rasterizerDiscardEnable;
        key.PolygonMode                 = static_cast<uint32_t>(rasterization.polygonMode);
        key.CullMode                    = rasterization.cullMode;
        key.FrontFace                   = static_cast<uint32_t>(rasterization.frontFace);
        key.DepthBiasEnable             = rasterization.depthBiasEnable;
        key.DepthBiasConstantFactorBits = std::bit_cast<uint32_t>(rasterization.depthBiasConstantFactor);
        key.DepthBiasClampBits          = std::bit_cast<uint32_t>(rasterization.depthBiasClamp);
        key.DepthBiasSlopeFactorBits    = std::bit_cast<uint32_t>(rasterization.depthBiasSlopeFactor);
        key.LineWidthBits               = std::bit_cast<uint32_t>(rasterization.lineWidth);
        key.MultisampleFlags            = multisample.flags;
        key.RasterizationSamples        = static_cast<uint32_t>(multisample.rasterizationSamples);
        key.SampleShadingEnable         = multisample.sampleShadingEnable;
        key.MinSampleShadingBits        = std::bit_cast<uint32_t>(multisample.minSampleShading);
        key.AlphaToCoverageEnable       = multisample.alphaToCoverageEnable;
        key.AlphaToOneEnable            = multisample.alphaToOneEnable;
        key.ColorBlendFlags             = color_blend.flags;
        key.LogicOpEnable               = color_blend.logicOpEnable;
        key.LogicOp                     = static_cast<uint32_t>(color_blend.logicOp);
        key.ColorBlendAttachmentCount   = color_blend.attachmentCount;
        key.UsesDynamicRendering        = rendering_info != nullptr ? VK_TRUE : VK_FALSE;
        if (rendering_info)
        {
            key.RenderingViewMask                = rendering_info->viewMask;
            key.RenderingColorAttachmentCount    = rendering_info->colorAttachmentCount;
            key.RenderingDepthAttachmentFormat   = static_cast<uint32_t>(rendering_info->depthAttachmentFormat);
            key.RenderingStencilAttachmentFormat = static_cast<uint32_t>(rendering_info->stencilAttachmentFormat);
            for (uint32_t index = 0; index < key.RenderingColorAttachmentCount; ++index)
                key.RenderingColorAttachmentFormats[index] = static_cast<uint32_t>(rendering_info->pColorAttachmentFormats[index]);
        }
        for (uint32_t i = 0; i < 4; ++i)
            key.BlendConstantBits[i] = std::bit_cast<uint32_t>(color_blend.blendConstants[i]);

        const uint64_t layout_identity = GetPipelineLayoutIdentity(create_info.layout);
        key.Layout.CachedIdentity      = layout_identity;
        if (layout_identity == 0)
            key.Layout.ExternalHandle = create_info.layout;
        if (!rendering_info)
        {
            const uint64_t render_pass_identity = GetCompatibilityRenderPassIdentity(create_info.renderPass);
            key.RenderPass.CachedIdentity       = render_pass_identity;
            if (render_pass_identity == 0)
                key.RenderPass.ExternalHandle = create_info.renderPass;
        }

        for (uint32_t i = 0; i < key.ShaderStageCount; ++i)
        {
            const auto& stage = create_info.pStages[i];
            ZENGINE_VALIDATE_ASSERT(stage.sType == VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO && stage.pNext == nullptr && stage.flags == 0, "Unsupported graphics shader stage create info")
            ZENGINE_VALIDATE_ASSERT(IsGraphicsShaderStage(stage.stage), "Unsupported graphics shader stage in PSO cache")
            ZENGINE_VALIDATE_ASSERT(stage.stage != VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT && stage.stage != VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "Tessellation shaders are not supported by the synchronous PSO cache")
            ZENGINE_VALIDATE_ASSERT(stage.module != VK_NULL_HANDLE, "Graphics pipeline shader module is null")
            ZENGINE_VALIDATE_ASSERT(stage.pName != nullptr && Helpers::secure_strcmp(stage.pName, "main") == 0, "Only the main graphics entry point is supported by PSO cache")
            key.ShaderStages[i] = {
                .ModuleIdentity = reinterpret_cast<uintptr_t>(stage.module),
                .Generation     = shader_generation,
                .Stage          = static_cast<uint32_t>(stage.stage),
                .Specialization = MakeSpecializationKey(stage.pSpecializationInfo),
            };
        }
        SortGraphicsShaderStages(key);

        for (uint32_t i = 0; i < key.VertexBindingCount; ++i)
        {
            const auto& binding = vertex_input.pVertexBindingDescriptions[i];
            ZENGINE_VALIDATE_ASSERT(binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX || binding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE, "Unsupported graphics vertex input rate")
            key.VertexBindings[i] = {
                .Binding   = binding.binding,
                .Stride    = binding.stride,
                .InputRate = static_cast<uint32_t>(binding.inputRate),
            };
        }
        SortVertexBindings(key);

        for (uint32_t i = 0; i < key.VertexAttributeCount; ++i)
        {
            const auto& attribute = vertex_input.pVertexAttributeDescriptions[i];
            ZENGINE_VALIDATE_ASSERT(attribute.format != VK_FORMAT_UNDEFINED, "Graphics pipeline vertex attribute format is undefined")
            key.VertexAttributes[i] = {
                .Location = attribute.location,
                .Binding  = attribute.binding,
                .Format   = static_cast<uint32_t>(attribute.format),
                .Offset   = attribute.offset,
            };
        }
        SortVertexAttributes(key);

        if (create_info.pDepthStencilState != nullptr)
        {
            const auto& depth_stencil = *create_info.pDepthStencilState;
            ZENGINE_VALIDATE_ASSERT(depth_stencil.sType == VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO && depth_stencil.pNext == nullptr, "Unsupported graphics depth-stencil state")
            key.HasDepthStencilState  = VK_TRUE;
            key.DepthStencilFlags     = depth_stencil.flags;
            key.DepthTestEnable       = depth_stencil.depthTestEnable;
            key.DepthWriteEnable      = depth_stencil.depthWriteEnable;
            key.DepthCompareOp        = static_cast<uint32_t>(depth_stencil.depthCompareOp);
            key.DepthBoundsTestEnable = depth_stencil.depthBoundsTestEnable;
            key.StencilTestEnable     = depth_stencil.stencilTestEnable;
            key.MinDepthBoundsBits    = std::bit_cast<uint32_t>(depth_stencil.minDepthBounds);
            key.MaxDepthBoundsBits    = std::bit_cast<uint32_t>(depth_stencil.maxDepthBounds);
            key.FrontStencil          = MakeStencilOpStateKey(depth_stencil.front);
            key.BackStencil           = MakeStencilOpStateKey(depth_stencil.back);
        }

        for (uint32_t i = 0; i < key.ColorBlendAttachmentCount; ++i)
            key.ColorBlendAttachments[i] = MakeColorBlendAttachmentKey(color_blend.pAttachments[i]);
        return key;
    }

    uint64_t PSOCache::HashSamplerKey(const PSOSamplerKey& key) const
    {
        uint64_t hash = HashAppend(0, key.CreateFlags);
        hash          = HashAppend(hash, key.MipLodBiasBits);
        hash          = HashAppend(hash, key.MaxAnisotropyBits);
        hash          = HashAppend(hash, key.MinLodBits);
        hash          = HashAppend(hash, key.MaxLodBits);
        hash          = HashAppend(hash, static_cast<uint32_t>(key.CompareOp));
        hash          = HashAppend(hash, static_cast<uint32_t>(key.BorderColor));
        hash          = HashAppend(hash, static_cast<uint32_t>(key.MagFilter));
        hash          = HashAppend(hash, static_cast<uint32_t>(key.MinFilter));
        hash          = HashAppend(hash, static_cast<uint32_t>(key.MipmapMode));
        hash          = HashAppend(hash, static_cast<uint32_t>(key.AddressModeU));
        hash          = HashAppend(hash, static_cast<uint32_t>(key.AddressModeV));
        hash          = HashAppend(hash, static_cast<uint32_t>(key.AddressModeW));
        hash          = HashAppend(hash, key.AnisotropyEnabled);
        hash          = HashAppend(hash, key.CompareEnabled);
        hash          = HashAppend(hash, key.UnnormalizedCoords);
        return hash;
    }

    uint64_t PSOCache::HashImmutableSamplerListKey(const PSOImmutableSamplerListKey& key) const
    {
        uint64_t hash = HashAppend(0, key.SamplerCount);
        for (uint32_t sampler_index = 0; sampler_index < key.SamplerCount; ++sampler_index)
            hash = HashAppend(hash, reinterpret_cast<uintptr_t>(key.Samplers[sampler_index]));
        return hash;
    }

    uint64_t PSOCache::HashDescriptorSetLayoutKey(const PSODescriptorSetLayoutKey& key) const
    {
        uint64_t hash = HashAppend(0, key.CreateFlags);
        hash          = HashAppend(hash, key.BindingCount);
        for (uint32_t i = 0; i < key.BindingCount; ++i)
        {
            const auto& binding = key.Bindings[i];
            hash                = HashAppend(hash, binding.Binding);
            hash                = HashAppend(hash, binding.DescriptorCount);
            hash                = HashAppend(hash, binding.ShaderStages);
            hash                = HashAppend(hash, binding.BindingFlags);
            hash                = HashAppend(hash, binding.ImmutableSamplerListIdentity);
            hash                = HashAppend(hash, static_cast<uint32_t>(binding.DescriptorKind));
        }
        return hash;
    }

    uint64_t PSOCache::HashPipelineLayoutKey(const PSOPipelineLayoutKey& key) const
    {
        uint64_t hash = HashAppend(0, key.SetLayoutCount);
        hash          = HashAppend(hash, key.PushConstantCount);
        for (uint32_t i = 0; i < key.SetLayoutCount; ++i)
        {
            hash = HashAppend(hash, key.SetLayouts[i].CachedIdentity);
            hash = HashAppend(hash, reinterpret_cast<uintptr_t>(key.SetLayouts[i].ExternalHandle));
        }
        for (uint32_t i = 0; i < key.PushConstantCount; ++i)
        {
            hash = HashAppend(hash, key.PushConstants[i].Offset);
            hash = HashAppend(hash, key.PushConstants[i].Size);
            hash = HashAppend(hash, key.PushConstants[i].ShaderStages);
        }
        return hash;
    }

    uint64_t PSOCache::HashCompatibilityRenderPassKey(const PSOCompatibilityRenderPassKey& key) const
    {
        uint64_t hash = HashAppend(0, key.AttachmentCount);
        hash          = HashAppend(hash, key.ColorAttachmentCount);
        hash          = HashAppend(hash, key.DepthAttachmentIndex);
        hash          = HashAppend(hash, key.ViewMask);
        for (uint32_t i = 0; i < key.AttachmentCount; ++i)
        {
            hash = HashAppend(hash, key.Attachments[i].Format);
            hash = HashAppend(hash, key.Attachments[i].Samples);
            hash = HashAppend(hash, key.Attachments[i].ReferenceLayout);
        }
        return hash;
    }

    uint64_t PSOCache::HashComputePipelineKey(const PSOComputePipelineKey& key) const
    {
        uint64_t hash = HashAppend(0, key.CreateFlags);
        hash          = HashAppend(hash, key.Shader.ModuleIdentity);
        hash          = HashAppend(hash, key.Shader.Generation);
        hash          = HashAppend(hash, static_cast<uint32_t>(key.Shader.Stage));
        hash          = HashSpecializationKey(hash, key.Shader.Specialization);
        hash          = HashAppend(hash, key.Layout.CachedIdentity);
        hash          = HashAppend(hash, reinterpret_cast<uintptr_t>(key.Layout.ExternalHandle));
        return hash;
    }

    uint64_t PSOCache::HashGraphicsPipelineKey(const PSOGraphicsPipelineKey& key) const
    {
        uint64_t hash = HashAppend(0, key.CreateFlags);
        hash          = HashAppend(hash, key.ShaderStageCount);
        hash          = HashAppend(hash, key.Subpass);
        hash          = HashAppend(hash, key.VertexBindingCount);
        hash          = HashAppend(hash, key.VertexAttributeCount);
        hash          = HashAppend(hash, key.InputAssemblyTopology);
        hash          = HashAppend(hash, key.PrimitiveRestartEnable);
        hash          = HashAppend(hash, key.ViewportCount);
        hash          = HashAppend(hash, key.ScissorCount);
        hash          = HashAppend(hash, key.RasterizationFlags);
        hash          = HashAppend(hash, key.DepthClampEnable);
        hash          = HashAppend(hash, key.RasterizerDiscardEnable);
        hash          = HashAppend(hash, key.PolygonMode);
        hash          = HashAppend(hash, key.CullMode);
        hash          = HashAppend(hash, key.FrontFace);
        hash          = HashAppend(hash, key.DepthBiasEnable);
        hash          = HashAppend(hash, key.DepthBiasConstantFactorBits);
        hash          = HashAppend(hash, key.DepthBiasClampBits);
        hash          = HashAppend(hash, key.DepthBiasSlopeFactorBits);
        hash          = HashAppend(hash, key.LineWidthBits);
        hash          = HashAppend(hash, key.MultisampleFlags);
        hash          = HashAppend(hash, key.RasterizationSamples);
        hash          = HashAppend(hash, key.SampleShadingEnable);
        hash          = HashAppend(hash, key.MinSampleShadingBits);
        hash          = HashAppend(hash, key.AlphaToCoverageEnable);
        hash          = HashAppend(hash, key.AlphaToOneEnable);
        hash          = HashAppend(hash, key.HasDepthStencilState);
        hash          = HashAppend(hash, key.DepthStencilFlags);
        hash          = HashAppend(hash, key.DepthTestEnable);
        hash          = HashAppend(hash, key.DepthWriteEnable);
        hash          = HashAppend(hash, key.DepthCompareOp);
        hash          = HashAppend(hash, key.DepthBoundsTestEnable);
        hash          = HashAppend(hash, key.StencilTestEnable);
        hash          = HashAppend(hash, key.MinDepthBoundsBits);
        hash          = HashAppend(hash, key.MaxDepthBoundsBits);
        hash          = HashAppend(hash, key.ColorBlendFlags);
        hash          = HashAppend(hash, key.LogicOpEnable);
        hash          = HashAppend(hash, key.LogicOp);
        hash          = HashAppend(hash, key.ColorBlendAttachmentCount);
        hash          = HashAppend(hash, key.UsesDynamicRendering);
        hash          = HashAppend(hash, key.RenderingViewMask);
        hash          = HashAppend(hash, key.RenderingColorAttachmentCount);
        hash          = HashAppend(hash, key.RenderingDepthAttachmentFormat);
        hash          = HashAppend(hash, key.RenderingStencilAttachmentFormat);
        hash          = HashAppend(hash, key.Layout.CachedIdentity);
        hash          = HashAppend(hash, reinterpret_cast<uintptr_t>(key.Layout.ExternalHandle));
        hash          = HashAppend(hash, key.RenderPass.CachedIdentity);
        hash          = HashAppend(hash, reinterpret_cast<uintptr_t>(key.RenderPass.ExternalHandle));
        for (uint32_t i = 0; i < 4; ++i)
            hash = HashAppend(hash, key.BlendConstantBits[i]);
        for (uint32_t i = 0; i < key.RenderingColorAttachmentCount; ++i)
            hash = HashAppend(hash, key.RenderingColorAttachmentFormats[i]);
        for (uint32_t i = 0; i < key.ShaderStageCount; ++i)
        {
            hash = HashAppend(hash, key.ShaderStages[i].ModuleIdentity);
            hash = HashAppend(hash, key.ShaderStages[i].Generation);
            hash = HashAppend(hash, key.ShaderStages[i].Stage);
            hash = HashSpecializationKey(hash, key.ShaderStages[i].Specialization);
        }
        for (uint32_t i = 0; i < key.VertexBindingCount; ++i)
        {
            hash = HashAppend(hash, key.VertexBindings[i].Binding);
            hash = HashAppend(hash, key.VertexBindings[i].Stride);
            hash = HashAppend(hash, key.VertexBindings[i].InputRate);
        }
        for (uint32_t i = 0; i < key.VertexAttributeCount; ++i)
        {
            hash = HashAppend(hash, key.VertexAttributes[i].Location);
            hash = HashAppend(hash, key.VertexAttributes[i].Binding);
            hash = HashAppend(hash, key.VertexAttributes[i].Format);
            hash = HashAppend(hash, key.VertexAttributes[i].Offset);
        }
        hash = HashAppend(hash, key.FrontStencil.FailOp);
        hash = HashAppend(hash, key.FrontStencil.PassOp);
        hash = HashAppend(hash, key.FrontStencil.DepthFailOp);
        hash = HashAppend(hash, key.FrontStencil.CompareOp);
        hash = HashAppend(hash, key.FrontStencil.CompareMask);
        hash = HashAppend(hash, key.FrontStencil.WriteMask);
        hash = HashAppend(hash, key.FrontStencil.Reference);
        hash = HashAppend(hash, key.BackStencil.FailOp);
        hash = HashAppend(hash, key.BackStencil.PassOp);
        hash = HashAppend(hash, key.BackStencil.DepthFailOp);
        hash = HashAppend(hash, key.BackStencil.CompareOp);
        hash = HashAppend(hash, key.BackStencil.CompareMask);
        hash = HashAppend(hash, key.BackStencil.WriteMask);
        hash = HashAppend(hash, key.BackStencil.Reference);
        for (uint32_t i = 0; i < key.ColorBlendAttachmentCount; ++i)
        {
            const auto& attachment = key.ColorBlendAttachments[i];
            hash                   = HashAppend(hash, attachment.BlendEnable);
            hash                   = HashAppend(hash, attachment.SrcColorBlendFactor);
            hash                   = HashAppend(hash, attachment.DstColorBlendFactor);
            hash                   = HashAppend(hash, attachment.ColorBlendOp);
            hash                   = HashAppend(hash, attachment.SrcAlphaBlendFactor);
            hash                   = HashAppend(hash, attachment.DstAlphaBlendFactor);
            hash                   = HashAppend(hash, attachment.AlphaBlendOp);
            hash                   = HashAppend(hash, attachment.ColorWriteMask);
        }
        return hash;
    }

    uint64_t PSOCache::GetSetLayoutIdentity(VkDescriptorSetLayout layout) const
    {
        DescriptorSetLayoutIdentityContext lookup = {};
        lookup.Handle                             = layout;
        m_descriptor_set_layouts.ForEach(&lookup, &FindDescriptorSetLayoutIdentity);
        return lookup.Identity;
    }

    uint64_t PSOCache::GetOrCreateImmutableSamplerList(const VkSampler* samplers, uint32_t sampler_count)
    {
        ZENGINE_VALIDATE_ASSERT(samplers != nullptr && sampler_count != 0 && sampler_count <= kMaxPSOImmutableSamplers, "PSO immutable sampler capacity exceeded")

        PSOImmutableSamplerListKey key = {};
        key.SamplerCount               = sampler_count;
        for (uint32_t sampler_index = 0; sampler_index < sampler_count; ++sampler_index)
        {
            ZENGINE_VALIDATE_ASSERT(samplers[sampler_index] != VK_NULL_HANDLE, "PSO immutable sampler is null")
            key.Samplers[sampler_index] = samplers[sampler_index];
        }

        const uint64_t             hash = HashImmutableSamplerListKey(key);
        bool                       created{};
        ImmutableSamplerListEntry* entry = m_immutable_sampler_lists.FindOrInsert(hash, key, &created);
        ZENGINE_VALIDATE_ASSERT(entry != nullptr, "PSO immutable sampler collision bucket capacity exceeded")
        if (!created)
            return entry->Identity;

        entry->Identity = m_next_immutable_sampler_list_identity++;
        for (uint32_t sampler_index = 0; sampler_index < sampler_count; ++sampler_index)
            entry->Samplers[sampler_index] = samplers[sampler_index];
        return entry->Identity;
    }

    const PSOCache::ImmutableSamplerListEntry* PSOCache::FindImmutableSamplerList(uint64_t identity) const
    {
        if (identity == 0)
            return nullptr;

        ImmutableSamplerListIdentityContext lookup = {};
        lookup.Identity                            = identity;
        m_immutable_sampler_lists.ForEach(&lookup, &FindImmutableSamplerListIdentity);
        return lookup.Entry;
    }

    uint64_t PSOCache::GetPipelineLayoutIdentity(VkPipelineLayout layout) const
    {
        PipelineLayoutIdentityContext lookup = {};
        lookup.Handle                        = layout;
        m_pipeline_layouts.ForEach(&lookup, &FindPipelineLayoutIdentity);
        return lookup.Identity;
    }

    uint64_t PSOCache::GetCompatibilityRenderPassIdentity(VkRenderPass render_pass) const
    {
        CompatibilityRenderPassIdentityContext lookup = {};
        lookup.Handle                                 = render_pass;
        m_compatibility_render_passes.ForEach(&lookup, &FindCompatibilityRenderPassIdentity);
        return lookup.Identity;
    }

    uint64_t PSOCache::GetCurrentFrameMarker() const
    {
        if (!m_device || !m_device->SwapchainPtr)
            return 0;
        return m_device->SwapchainPtr->RenderTimelineNextValue;
    }

    void PSOCache::RetirePipeline(VkPipeline pipeline) const
    {
        if (pipeline == VK_NULL_HANDLE)
            return;

        Hardwares::DeferredFreeEntry entry = {};
        entry.EntryKind                    = Hardwares::DeferredFreeEntry::Kind::VkHandle;
        entry.Data.Vk                      = {reinterpret_cast<void*>(pipeline), DeviceResourceType::PIPELINE, nullptr};
        m_device->DeferFree(entry);
    }
} // namespace ZEngine::Rendering::Renderers::Pipelines
