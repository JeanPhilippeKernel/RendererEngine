#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Renderers/Compute/FrustumCullingPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GizmoPass.h>
#include <ZEngine/Rendering/Renderers/Pipelines/PSOCache.h>
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <gtest/gtest.h>
#include "../Misc/SaturatedBackgroundPool.h"

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Rendering::Renderers::Pipelines;
using namespace ZEngine::Rendering::Renderers;
using ZEngine::Rendering::Renderers::RenderPasses::Attachment;
using ZEngine::Rendering::Specifications::AttachmentSpecification;
using ZEngine::Rendering::Specifications::ImageFormat;
using ZEngine::Rendering::Specifications::ImageLayout;

namespace ZEngine::Rendering::Renderers::Pipelines
{
    struct PSOCacheTestAccess
    {
        static void SeedEmptyPipelineLayout(PSOCache& cache)
        {
            const PSOPipelineLayoutKey key     = {};
            bool                       created = false;
            auto*                      entry   = cache.m_pipeline_layouts.FindOrInsert(cache.HashPipelineLayoutKey(key), key, &created);
            ASSERT_NE(entry, nullptr);
            ASSERT_TRUE(created);
            entry->Handle   = reinterpret_cast<VkPipelineLayout>(uintptr_t(2));
            entry->Identity = 1;
        }

        static uint32_t PipelinePins(const PSOCache& cache, VkPipeline handle)
        {
            struct PinLookup
            {
                VkPipeline Handle = VK_NULL_HANDLE;
                uint32_t   Count  = 0;
            } lookup             = {handle};
            const auto find_pins = [](void* context, const auto& entry) {
                auto& lookup = *static_cast<PinLookup*>(context);
                if (entry.Handle == lookup.Handle)
                {
                    lookup.Count += entry.PinCount;
                }
            };
            cache.m_compute_pipelines.ForEach(&lookup, find_pins);
            cache.m_graphics_pipelines.ForEach(&lookup, find_pins);
            return lookup.Count;
        }

        static void EnablePreparation(PSOCache& cache)
        {
            cache.m_accept_async_jobs           = true;
            cache.m_worker_pipeline_cache_count = 1;
            // No worker-cache lease: jobs remain queued without invoking Vulkan.
            cache.m_available_worker_pipeline_caches.value.store(0);
        }

        static void SetPreparationAvailability(PSOCache& cache, bool accept, uint32_t worker_count)
        {
            cache.m_accept_async_jobs           = accept;
            cache.m_worker_pipeline_cache_count = worker_count;
        }

        static void ResolvePreparation(PSOCache& cache, VkResult result, VkPipeline pipeline = VK_NULL_HANDLE, bool cancelled = false)
        {
            auto& job = cache.m_async_jobs[0];
            ASSERT_EQ(job.State, PSOCache::AsyncPipelineJobState::Queued);
            job.State                                    = cancelled ? PSOCache::AsyncPipelineJobState::Cancelled : PSOCache::AsyncPipelineJobState::Running;
            PSOCache::AsyncPipelineCompletion completion = {};
            completion.Result                            = result;
            completion.Pipeline                          = pipeline;
            cache.ResolveAsyncPipelineResult(job, completion);
            cache.DispatchPipelineNotifications();
            EXPECT_EQ(job.State, PSOCache::AsyncPipelineJobState::Free);
        }

        static void SaturateJobs(PSOCache& cache)
        {
            for (auto& job : cache.m_async_jobs)
            {
                job.State = PSOCache::AsyncPipelineJobState::Queued;
            }
        }

        static void ReleaseJobs(PSOCache& cache)
        {
            for (auto& job : cache.m_async_jobs)
            {
                job = {};
            }
        }

        static void SaturateBuckets(PSOCache& cache)
        {
            for (uint32_t i = 0; i < kPSOComputePipelineEntriesPerBucket; ++i)
            {
                PSOComputePipelineKey key = {};
                key.Shader.Generation     = i;
                for (uint32_t bucket = 0; bucket < kPSOComputePipelineBucketCount; ++bucket)
                {
                    bool created = false;
                    ASSERT_NE(cache.m_compute_pipelines.FindOrInsert(bucket, key, &created), nullptr);
                    ASSERT_TRUE(created);
                }
            }
            for (uint32_t i = 0; i < kPSOGraphicsPipelineEntriesPerBucket; ++i)
            {
                PSOGraphicsPipelineKey key = {};
                key.Subpass                = i;
                for (uint32_t bucket = 0; bucket < kPSOGraphicsPipelineBucketCount; ++bucket)
                {
                    bool created = false;
                    ASSERT_NE(cache.m_graphics_pipelines.FindOrInsert(bucket, key, &created), nullptr);
                    ASSERT_TRUE(created);
                }
            }
        }

        static uint32_t QueuedWaiters(const PSOCache& cache)
        {
            uint32_t   count         = 0;
            const auto count_waiters = [](void* context, const auto& entry) { *static_cast<uint32_t*>(context) += entry.WaiterCount; };
            cache.m_compute_pipelines.ForEach(&count, count_waiters);
            cache.m_graphics_pipelines.ForEach(&count, count_waiters);
            return count;
        }

        static void CheckSaturatedAsyncDispatch(PSOCache& cache)
        {
            cache.m_accept_async_jobs           = true;
            cache.m_worker_pipeline_cache_count = 1;
            cache.m_available_worker_pipeline_caches.value.store(1);
            auto& job = cache.m_async_jobs[0];
            job.State = PSOCache::AsyncPipelineJobState::Queued;
            cache.DispatchQueuedAsyncPipelineJobs();
            EXPECT_EQ(job.State, PSOCache::AsyncPipelineJobState::Queued);
            EXPECT_EQ(job.WorkerCacheIndex, UINT32_MAX);
            EXPECT_EQ(cache.m_active_async_jobs.value.load(), 0u);
            EXPECT_EQ(cache.m_available_worker_pipeline_caches.value.load(), 1u);
        }
        static void SetDevice(PSOCache& cache, ZEngine::Hardwares::VulkanDevice* device)
        {
            cache.m_device = device;
        }

        static void SaturatePipelineLayouts(PSOCache& cache)
        {
            for (uint32_t bucket = 0; bucket < kPSOPipelineLayoutBucketCount; ++bucket)
            {
                for (uint32_t i = 0; i < kPSOPipelineLayoutEntriesPerBucket; ++i)
                {
                    PSOPipelineLayoutKey key = {.PushConstantCount = 1};
                    key.PushConstants[0]     = {.Offset = i * 4, .Size = 4, .ShaderStages = VK_SHADER_STAGE_VERTEX_BIT};
                    bool created             = false;
                    cache.m_pipeline_layouts.FindOrInsert(bucket, key, &created);
                }
            }
        }

        static void ReleaseEmptyPipelineLayouts(PSOCache& cache)
        {
            cache.m_pipeline_layouts.RemoveIf(nullptr, [](void*, const auto&, auto& entry) { return entry.Handle == VK_NULL_HANDLE; });
        }

        static void SaturateCompatibilityRenderPasses(PSOCache& cache)
        {
            for (uint32_t bucket = 0; bucket < kPSOCompatibilityRenderPassBucketCount; ++bucket)
            {
                for (uint32_t i = 0; i < kPSOCompatibilityRenderPassEntriesPerBucket; ++i)
                {
                    PSOCompatibilityRenderPassKey key = {.AttachmentCount = 1, .ColorAttachmentCount = 1};
                    key.Attachments[0].Format         = i + 1;
                    bool created                      = false;
                    cache.m_compatibility_render_passes.FindOrInsert(bucket, key, &created);
                }
            }
        }

        static void ReleaseEmptyCompatibilityRenderPasses(PSOCache& cache)
        {
            cache.m_compatibility_render_passes.RemoveIf(nullptr, [](void*, const auto&, auto& entry) { return entry.Handle == VK_NULL_HANDLE; });
        }

        static uint32_t ImmutableSamplerLists(const PSOCache& cache)
        {
            uint32_t count = 0;
            cache.m_immutable_sampler_lists.ForEach(&count, [](void* context, const auto&) { ++*static_cast<uint32_t*>(context); });
            return count;
        }

        static void SaturateDescriptorLayouts(PSOCache& cache)
        {
            for (uint32_t bucket = 0; bucket < kPSOLayoutBucketCount; ++bucket)
            {
                for (uint32_t i = 0; i < kPSOLayoutEntriesPerBucket; ++i)
                {
                    PSODescriptorSetLayoutKey key = {.BindingCount = 1};
                    key.Bindings[0]               = {.Binding = i, .DescriptorCount = 1, .ShaderStages = VK_SHADER_STAGE_VERTEX_BIT, .DescriptorKind = PSODescriptorKind::UniformBuffer};
                    bool created                  = false;
                    cache.m_descriptor_set_layouts.FindOrInsert(bucket, key, &created);
                }
            }
        }

        static void ReleaseEmptyDescriptorLayouts(PSOCache& cache)
        {
            cache.m_descriptor_set_layouts.RemoveIf(nullptr, [](void*, const auto&, auto& entry) { return entry.Handle == VK_NULL_HANDLE; });
        }

        static uint64_t ImmutableSamplerBucket(PSOCache& cache, VkSampler sampler)
        {
            PSOImmutableSamplerListKey key = {.SamplerCount = 1};
            key.Samplers[0]                = sampler;
            return cache.HashImmutableSamplerListKey(key) % kPSOImmutableSamplerListBucketCount;
        }

        static void SaturateImmutableSamplerBucket(PSOCache& cache, VkSampler sampler)
        {
            const auto bucket = ImmutableSamplerBucket(cache, sampler);
            for (uint32_t i = 0; i < kPSOImmutableSamplerListEntriesPerBucket; ++i)
            {
                PSOImmutableSamplerListKey key = {.SamplerCount = 1};
                key.Samplers[0]                = reinterpret_cast<VkSampler>(uintptr_t(65536 + i));
                bool  created                  = false;
                auto* entry                    = cache.m_immutable_sampler_lists.FindOrInsert(bucket, key, &created);
                if (entry && created)
                {
                    entry->Identity = UINT64_MAX;
                }
            }
        }

        static void ReleaseSaturatedImmutableSamplerLists(PSOCache& cache)
        {
            cache.m_immutable_sampler_lists.RemoveIf(nullptr, [](void*, const auto&, auto& entry) { return entry.Identity == UINT64_MAX; });
        }

        static void AddWarmupRecipe(PSOCache& cache, const PSOWarmupRecipe& recipe)
        {
            cache.RecordWarmupRecipe(recipe);
        }

        static uint32_t WarmupRecipeCount(const PSOCache& cache)
        {
            return cache.m_warmup_recipe_count;
        }

        static void SetWarmupVFS(PSOCache& cache, IVFSContext* context)
        {
            cache.m_warmup_vfs = context;
        }

        static bool RetainsPinnedInvalidatedComputePipeline()
        {
            const VkShaderModule  module                     = reinterpret_cast<VkShaderModule>(uintptr_t(1));
            const VkPipeline      pipeline                   = reinterpret_cast<VkPipeline>(uintptr_t(2));
            PSOComputePipelineKey key                        = {};
            key.Shader.ModuleIdentity                        = reinterpret_cast<uintptr_t>(module);
            key.Shader.Generation                            = 7;

            PSOCache::ComputePipelineEntry entry             = {};
            entry.Handle                                     = pipeline;
            entry.PinCount                                   = 1;
            entry.State                                      = PSOPipelineState::Ready;

            PSOCache::ShaderInvalidationContext invalidation = {};
            invalidation.ShaderModules                       = &module;
            invalidation.ModuleCount                         = 1;
            invalidation.Generation                          = 7;
            const bool removed                               = PSOCache::RemoveInvalidatedComputePipeline(&invalidation, key, entry);

            return !removed && entry.Handle == pipeline && entry.PinCount == 1;
        }

        static bool RetainsPinnedInvalidatedGraphicsPipeline()
        {
            const VkShaderModule   module                    = reinterpret_cast<VkShaderModule>(uintptr_t(3));
            const VkPipeline       pipeline                  = reinterpret_cast<VkPipeline>(uintptr_t(4));
            PSOGraphicsPipelineKey key                       = {};
            key.ShaderStageCount                             = 1;
            key.ShaderStages[0].ModuleIdentity               = reinterpret_cast<uintptr_t>(module);
            key.ShaderStages[0].Generation                   = 11;
            key.ShaderStages[0].Stage                        = VK_SHADER_STAGE_VERTEX_BIT;

            PSOCache::GraphicsPipelineEntry entry            = {};
            entry.Handle                                     = pipeline;
            entry.PinCount                                   = 1;
            entry.State                                      = PSOPipelineState::Ready;

            PSOCache::ShaderInvalidationContext invalidation = {};
            invalidation.ShaderModules                       = &module;
            invalidation.ModuleCount                         = 1;
            invalidation.Generation                          = 11;
            const bool removed                               = PSOCache::RemoveInvalidatedGraphicsPipeline(&invalidation, key, entry);

            return !removed && entry.Handle == pipeline && entry.PinCount == 1;
        }
    };
} // namespace ZEngine::Rendering::Renderers::Pipelines

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
            Cache = ZPushStructCtor<PSOCache>(&Manager.MainArena);
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

    struct WarmupRecipeFile : IVFSFile
    {
        enum class PayloadReadMode : uint8_t
        {
            Complete,
            Short,
            Failure,
        };

        IVFSFile*         File     = nullptr;
        PayloadReadMode   ReadMode = PayloadReadMode::Complete;

        VFSResult<size_t> Read(ZEngine::Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset) override
        {
            if (offset >= 64 && ReadMode != PayloadReadMode::Complete)
            {
                if (ReadMode == PayloadReadMode::Failure)
                    return VFSResult<size_t>::Fail(VFSError::IOError);
                return File->Read({buffer.data(), buffer.size() / 2}, offset);
            }
            return File->Read(buffer, offset);
        }

        VFSResult<size_t> Write(ZEngine::Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset) override
        {
            return File->Write(buffer, offset);
        }
        VFSResult<uint64_t> Size() const override
        {
            return File->Size();
        }
        VFSResult<VFSFileStat> Stat() const override
        {
            return File->Stat();
        }
        VFSResult<void> Flush() override
        {
            return File->Flush();
        }
        const VFSPath& Path() const override
        {
            return File->Path();
        }
        VFSResult<void> Close() override
        {
            return File->Close();
        }
    };

    // Simulate persistent shader allocations while resolving a warmup recipe,
    // without requiring a Vulkan device or a real shader module.
    struct WarmupAllocationContext : VFSContext
    {
        static constexpr uint64_t kPersistentValue       = 0x680000000068;

        ArenaAllocator*           DeviceArena            = nullptr;
        ArenaAllocator            ShaderArena            = {};
        uint64_t*                 PersistentValue        = nullptr;
        size_t                    ExpectedCurrentOffset  = 0;
        size_t                    ExpectedPreviousOffset = 0;
        WarmupRecipeFile          RecipeFile             = {};
        uint32_t                  ShaderReadOpens        = 0;

        VFSResult<IVFSFile*>      Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            if (HasFlag(flags, VFSOpenFlags::Read) && path.Extension().Equals(".spv"))
            {
                ++ShaderReadOpens;
            }
            if (DeviceArena && !PersistentValue && HasFlag(flags, VFSOpenFlags::Read) && path.Extension().Equals(".spv"))
            {
                DeviceArena->CreateSubArena(ZKilo(4), &ShaderArena, "PSOCacheTest/Shader");
                PersistentValue        = ZPushStruct<uint64_t>(&ShaderArena);
                *PersistentValue       = kPersistentValue;
                ExpectedCurrentOffset  = DeviceArena->m_current_offset;
                ExpectedPreviousOffset = DeviceArena->m_previous_offset;
            }
            auto opened = VFSContext::Open(path, flags);
            if (opened.Succeeded() && HasFlag(flags, VFSOpenFlags::Read) && path.Extension().Equals(".bin"))
            {
                RecipeFile.File = opened.Value();
                return VFSResult<IVFSFile*>::Ok(&RecipeFile);
            }
            return opened;
        }

        void Close(IVFSFile* file) override
        {
            if (file == &RecipeFile)
            {
                VFSContext::Close(RecipeFile.File);
                RecipeFile.File = nullptr;
                return;
            }
            VFSContext::Close(file);
        }
    };
} // namespace

TEST(PSOCacheAdmissionTest, InvalidLayoutRecipesPreserveOutputAndDoNotEnterNativeCreation)
{
    PSOCacheTestStorage  storage        = {};
    auto&                cache          = storage.Get();
    PSOPipelineLayoutKey output         = {.SetLayoutCount = 1};
    output.SetLayouts[0].CachedIdentity = 91;
    const auto          original        = output;
    VkPushConstantRange ranges[2]       = {
        {  VK_SHADER_STAGE_VERTEX_BIT, 0, 4},
        {VK_SHADER_STAGE_FRAGMENT_BIT, 4, 4}
    };
    VkDescriptorSetLayout sets[kMaxPSOSetLayouts] = {};
    for (auto& set : sets)
    {
        set = reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(1));
    }
    const VkPipelineLayoutCreateInfo valid = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = kMaxPSOSetLayouts, .pSetLayouts = sets, .pushConstantRangeCount = 2, .pPushConstantRanges = ranges};
    ASSERT_TRUE(cache.TryMakePipelineLayoutKey(valid, &output));
    EXPECT_EQ(output.SetLayoutCount, kMaxPSOSetLayouts);
    const auto reject = [&](const VkPipelineLayoutCreateInfo& info) {
        output = original;
        EXPECT_FALSE(cache.TryMakePipelineLayoutKey(info, &output));
        EXPECT_EQ(output, original);
    };
    auto invalid           = valid;
    invalid.setLayoutCount = kMaxPSOSetLayouts + 1;
    reject(invalid);
    invalid                        = valid;
    invalid.pushConstantRangeCount = kMaxPSOPushConstantRanges + 1;
    reject(invalid);
    invalid             = valid;
    invalid.pSetLayouts = nullptr;
    reject(invalid);
    invalid                     = valid;
    invalid.pPushConstantRanges = nullptr;
    reject(invalid);
    invalid       = valid;
    invalid.flags = VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
    reject(invalid);
    invalid       = valid;
    invalid.pNext = &valid;
    reject(invalid);
    invalid       = valid;
    invalid.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    reject(invalid);
    sets[0] = VK_NULL_HANDLE;
    reject(valid);
    sets[0] = sets[1];
    for (const auto range : {
         VkPushConstantRange{  VK_SHADER_STAGE_VERTEX_BIT,              4, 4},
         VkPushConstantRange{                           0,              4, 4},
         VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT,              1, 4},
         VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT,              4, 1},
         VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, UINT32_MAX - 3, 4},
         VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT,              4, 0},
         VkPushConstantRange{         VK_SHADER_STAGE_ALL,              4, 4}
    })
    {
        ranges[1] = range;
        reject(valid);
    }
    EXPECT_FALSE(cache.TryMakePipelineLayoutKey(valid, nullptr));
}

TEST(PSOCacheAdmissionTest, DescriptorLayoutValidationIsBoundedAndPreservesOutputWithoutReservingMetadata)
{
    PSOCacheTestStorage          storage                         = {};
    auto&                        cache                           = storage.Get();
    VkDescriptorSetLayoutBinding bindings[kMaxPSOLayoutBindings] = {};
    VkDescriptorBindingFlags     flags[kMaxPSOLayoutBindings]    = {};
    for (uint32_t i = 0; i < kMaxPSOLayoutBindings; ++i)
    {
        bindings[i] = {.binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo extension = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, .bindingCount = kMaxPSOLayoutBindings, .pBindingFlags = flags};
    const VkDescriptorSetLayoutCreateInfo       valid     = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = &extension, .bindingCount = kMaxPSOLayoutBindings, .pBindings = bindings};
    PSODescriptorSetLayoutKey                   output    = {};
    VkResult                                    status    = VK_SUCCESS;
    ASSERT_TRUE(cache.TryMakeDescriptorSetLayoutKey(valid, &output, &status));
    EXPECT_EQ(output.BindingCount, kMaxPSOLayoutBindings);
    const auto      original = output;
    const VkSampler sampler  = reinterpret_cast<VkSampler>(uintptr_t(17));
    for (uint32_t fault = 0; fault < 18; ++fault)
    {
        auto       invalid = valid;
        const auto first   = bindings[0];
        const auto ext     = extension;
        switch (fault)
        {
            case 0:
                invalid.bindingCount = kMaxPSOLayoutBindings + 1;
                break;
            case 1:
                invalid.pBindings = nullptr;
                break;
            case 2:
                invalid.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
                break;
            case 3:
                invalid.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
                break;
            case 4:
                extension.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
                break;
            case 5:
                extension.pNext = &valid;
                break;
            case 6:
                --extension.bindingCount;
                break;
            case 7:
                extension.pBindingFlags = nullptr;
                break;
            case 8:
                bindings[0].descriptorCount = 0;
                break;
            case 9:
                bindings[0].stageFlags = 0;
                break;
            case 10:
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
                break;
            case 11:
                bindings[0].binding = bindings[1].binding;
                break;
            case 12:
                bindings[0].pImmutableSamplers = &sampler;
                break;
            case 13:
                bindings[0].pImmutableSamplers = &sampler;
                bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
                bindings[0].descriptorCount    = kMaxPSOImmutableSamplers + 1;
                break;
            case 14:
                flags[0] = 0x80000000u;
                break;
            case 15:
                flags[0] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
                break;
            case 16:
                flags[0] = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
                break;
            case 17:
                bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                invalid.flags              = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
                break;
        }
        EXPECT_FALSE(cache.TryMakeDescriptorSetLayoutKey(invalid, &output, &status)) << fault;
        EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED) << fault;
        EXPECT_EQ(output, original) << fault;
        EXPECT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), 0u);
        bindings[0] = first;
        extension   = ext;
        flags[0]    = 0;
    }
    EXPECT_FALSE(cache.TryMakeDescriptorSetLayoutKey(valid, nullptr, &status));
}

TEST(PSOCacheAdmissionTest, DescriptorNativeFailureAndMetadataSaturationRollBackOnlyUnpublishedReservations)
{
    struct Native
    {
        static VKAPI_ATTR VkResult VKAPI_CALL Success(VkDevice, const VkDescriptorSetLayoutCreateInfo* info, const VkAllocationCallbacks*, VkDescriptorSetLayout* out)
        {
            EXPECT_EQ(info->bindingCount, 2u);
            EXPECT_EQ(info->pBindings[0].binding, 1u);
            EXPECT_EQ(info->pBindings[1].binding, 9u);
            EXPECT_NE(info->pBindings[0].pImmutableSamplers, nullptr);
            EXPECT_NE(info->pBindings[1].pImmutableSamplers, nullptr);
            *out = reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(23));
            return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL OutOfMemory(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*)
        {
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL DeviceLost(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*)
        {
            return VK_ERROR_DEVICE_LOST;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Unexpected(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*)
        {
            ADD_FAILURE() << "Native creation must not run on refusal or cache hit";
            return VK_ERROR_UNKNOWN;
        }
    };
    PSOCacheTestStorage storage = {};
    auto&               cache   = storage.Get();
    auto                device  = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->LogicalDevice       = reinterpret_cast<VkDevice>(uintptr_t(1));
    PSOCacheTestAccess::SetDevice(cache, device.get());
    const VkSampler outgoing_samplers[2]  = {reinterpret_cast<VkSampler>(uintptr_t(1001)), reinterpret_cast<VkSampler>(uintptr_t(1002))};
    VkSampler       candidate_samplers[2] = {reinterpret_cast<VkSampler>(uintptr_t(2001)), reinterpret_cast<VkSampler>(uintptr_t(2002))};
    // Make the second list's bucket distinct, so a refusal there proves that
    // metadata reserved for the first list is rolled back as well.
    for (uintptr_t value = 2002; PSOCacheTestAccess::ImmutableSamplerBucket(cache, candidate_samplers[0]) == PSOCacheTestAccess::ImmutableSamplerBucket(cache, candidate_samplers[1]); ++value)
    {
        candidate_samplers[1] = reinterpret_cast<VkSampler>(value + 1);
        ASSERT_LT(value, 3000u);
    }
    const VkDescriptorSetLayoutBinding outgoing_bindings[2] = {
        {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &outgoing_samplers[0]},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &outgoing_samplers[1]}
    };
    const VkDescriptorSetLayoutBinding candidate_bindings[2] = {
        {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &candidate_samplers[0]},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &candidate_samplers[1]}
    };
    const VkDescriptorSetLayoutCreateInfo old_recipe = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = outgoing_bindings};
    auto                                  candidate  = old_recipe;
    candidate.pBindings                              = candidate_bindings;
    VkResult   status                                = VK_SUCCESS;
    const auto outgoing                              = cache.TryGetOrCreateDescriptorSetLayout(old_recipe, &status, Native::Success);
    ASSERT_NE(outgoing, VK_NULL_HANDLE);
    ASSERT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), 2u);
    for (uint32_t i = 0; i <= kPSOLayoutEntriesPerBucket; ++i)
    {
        EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(candidate, &status, Native::OutOfMemory), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
        EXPECT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), 2u);
    }
    PSOCacheTestAccess::SaturateDescriptorLayouts(cache);
    EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(candidate, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_TOO_MANY_OBJECTS);
    EXPECT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), 2u);
    EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(old_recipe, &status, Native::Unexpected), outgoing);
    PSOCacheTestAccess::ReleaseEmptyDescriptorLayouts(cache);
    PSOCacheTestAccess::SaturateImmutableSamplerBucket(cache, candidate_samplers[1]);
    const auto                saturated_count = PSOCacheTestAccess::ImmutableSamplerLists(cache);
    PSODescriptorSetLayoutKey key             = {.CreateFlags = 123};
    const auto                original        = key;
    EXPECT_FALSE(cache.TryMakeDescriptorSetLayoutKey(candidate, &key, &status));
    EXPECT_EQ(status, VK_ERROR_TOO_MANY_OBJECTS);
    EXPECT_EQ(key, original);
    EXPECT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), saturated_count);
    EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(old_recipe, &status, Native::Unexpected), outgoing);
    PSOCacheTestAccess::ReleaseSaturatedImmutableSamplerLists(cache);
    const VkDescriptorBindingFlags                    flags[2]  = {VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, 0};
    const VkDescriptorSetLayoutBindingFlagsCreateInfo extension = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, .bindingCount = 2, .pBindingFlags = flags};
    candidate.pNext                                             = &extension;
    EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(candidate, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_FEATURE_NOT_PRESENT);
    EXPECT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), 2u);
    candidate.pNext = nullptr;
    EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(candidate, &status, Native::DeviceLost), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(device->IsDeviceLost.load());
    EXPECT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), 2u);
    EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(old_recipe, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    device->IsDeviceLost.store(false); // Fault injection, not a real Vulkan device.
    EXPECT_NE(cache.TryGetOrCreateDescriptorSetLayout(candidate, &status, Native::Success), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(PSOCacheTestAccess::ImmutableSamplerLists(cache), 4u);
    EXPECT_EQ(cache.TryGetOrCreateDescriptorSetLayout(old_recipe, &status, Native::Unexpected), outgoing);
    device->LogicalDevice = VK_NULL_HANDLE;
}

TEST(PSOCacheAdmissionTest, LayoutNativeFailuresReleaseReservationAndCapacityRefusalPreservesOutgoing)
{
    struct Native
    {
        static VKAPI_ATTR VkResult VKAPI_CALL Success(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout* out)
        {
            *out = reinterpret_cast<VkPipelineLayout>(uintptr_t(17));
            return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL OutOfMemory(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*)
        {
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL DeviceLost(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*)
        {
            return VK_ERROR_DEVICE_LOST;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Unexpected(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*)
        {
            ADD_FAILURE() << "Native creation must not run on failed admission or a cache hit";
            return VK_ERROR_UNKNOWN;
        }
    };
    PSOCacheTestStorage              storage = {};
    auto                             device  = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    auto&                            cache   = storage.Get();
    VkResult                         status  = VK_SUCCESS;
    const VkPipelineLayoutCreateInfo empty   = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(empty, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    device->LogicalDevice = reinterpret_cast<VkDevice>(uintptr_t(1));
    PSOCacheTestAccess::SetDevice(cache, device.get());
    // More failures than one bucket can hold: every refused reservation must
    // be removed, or a subsequent retry would incorrectly hit a null handle.
    for (uint32_t i = 0; i <= kPSOPipelineLayoutEntriesPerBucket; ++i)
    {
        EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(empty, &status, Native::OutOfMemory), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    const auto outgoing = cache.TryGetOrCreatePipelineLayout(empty, &status, Native::Success);
    ASSERT_NE(outgoing, VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(empty, &status, Native::Unexpected), outgoing);
    PSOCacheTestAccess::SaturatePipelineLayouts(cache);
    const VkPushConstantRange range     = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    auto                      candidate = empty;
    candidate.pushConstantRangeCount    = 1;
    candidate.pPushConstantRanges       = &range;
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(candidate, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_TOO_MANY_OBJECTS);
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(empty, &status, Native::Unexpected), outgoing);
    PSOCacheTestAccess::ReleaseEmptyPipelineLayouts(cache);
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(candidate, &status, Native::DeviceLost), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(device->IsDeviceLost.load());
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(empty, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    device->IsDeviceLost.store(false); // Injection only, no real native device.
    EXPECT_NE(cache.TryGetOrCreatePipelineLayout(candidate, &status, Native::Success), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(empty, &status, Native::Unexpected), outgoing);
    auto invalid_range            = range;
    invalid_range.size            = 132;
    candidate.pPushConstantRanges = &invalid_range;
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(candidate, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    const VkDescriptorSetLayout sets[2]                                       = {reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(1)), reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(2))};
    candidate                                                                 = empty;
    candidate.setLayoutCount                                                  = 2;
    candidate.pSetLayouts                                                     = sets;
    device->PhysicalDeviceProperties.properties.limits.maxBoundDescriptorSets = 1;
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(candidate, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    EXPECT_EQ(cache.TryGetOrCreatePipelineLayout(empty, &status, Native::Unexpected), outgoing);
    device->LogicalDevice = VK_NULL_HANDLE;
}

TEST(PSOCacheAdmissionTest, CompatibilityNativeFailuresAndSaturationPreserveOutgoingAndPermitRetry)
{
    struct Native
    {
        static VKAPI_ATTR VkResult VKAPI_CALL Success(VkDevice, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks*, VkRenderPass* out)
        {
            EXPECT_EQ(info->attachmentCount, kMaxPSOCompatibilityAttachments);
            EXPECT_EQ(info->pSubpasses[0].colorAttachmentCount, kMaxPSOColorBlendAttachments);
            EXPECT_EQ(info->pSubpasses[0].pDepthStencilAttachment->attachment, 8u);
            *out = reinterpret_cast<VkRenderPass>(uintptr_t(19));
            return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL OutOfMemory(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*)
        {
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL DeviceLost(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*)
        {
            return VK_ERROR_DEVICE_LOST;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Unexpected(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*)
        {
            ADD_FAILURE() << "Native creation must not run on failed admission or a cache hit";
            return VK_ERROR_UNKNOWN;
        }
    };
    PSOCacheTestStorage           storage = {};
    auto                          device  = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    auto&                         cache   = storage.Get();
    PSOCompatibilityRenderPassKey key     = {.AttachmentCount = 9, .ColorAttachmentCount = 8, .DepthAttachmentIndex = 8};
    for (uint32_t i = 0; i < 8; ++i)
    {
        key.Attachments[i] = {.Format = VK_FORMAT_R8G8B8A8_UNORM, .ReferenceLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    key.Attachments[8] = {.Format = VK_FORMAT_D32_SFLOAT, .ReferenceLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkResult status    = VK_SUCCESS;
    EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(key, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    PSOCacheTestAccess::SetDevice(cache, device.get());
    device->LogicalDevice = reinterpret_cast<VkDevice>(uintptr_t(1));
    for (uint32_t i = 0; i <= kPSOCompatibilityRenderPassEntriesPerBucket; ++i)
    {
        EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(key, &status, Native::OutOfMemory), VK_NULL_HANDLE);
        EXPECT_EQ(status, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    const auto outgoing = cache.TryGetOrCreateCompatibilityRenderPass(key, &status, Native::Success);
    ASSERT_NE(outgoing, VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(key, &status, Native::Unexpected), outgoing);
    auto candidate                  = key;
    candidate.Attachments[0].Format = VK_FORMAT_R16G16B16A16_SFLOAT;
    PSOCacheTestAccess::SaturateCompatibilityRenderPasses(cache);
    EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(candidate, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_TOO_MANY_OBJECTS);
    EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(key, &status, Native::Unexpected), outgoing);
    PSOCacheTestAccess::ReleaseEmptyCompatibilityRenderPasses(cache);
    EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(candidate, &status, Native::DeviceLost), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    EXPECT_TRUE(device->IsDeviceLost.load());
    EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(key, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_DEVICE_LOST);
    device->IsDeviceLost.store(false); // Injected loss, not a real Vulkan device.
    EXPECT_NE(cache.TryGetOrCreateCompatibilityRenderPass(candidate, &status, Native::Success), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_SUCCESS);
    for (uint32_t fault = 0; fault < 10; ++fault)
    {
        auto invalid = key;
        switch (fault)
        {
            case 0:
                invalid.AttachmentCount = kMaxPSOCompatibilityAttachments + 1;
                break;
            case 1:
                invalid.ColorAttachmentCount = 9;
                break;
            case 2:
                invalid.DepthAttachmentIndex = 9;
                break;
            case 3:
                invalid.ViewMask = 1;
                break;
            case 4:
                invalid.Attachments[0].Format = VK_FORMAT_UNDEFINED;
                break;
            case 5:
                invalid.Attachments[0].Format = UINT32_MAX;
                break;
            case 6:
                invalid.Attachments[0].Samples = VK_SAMPLE_COUNT_2_BIT;
                break;
            case 7:
                invalid.Attachments[8].Format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
            case 8:
                invalid.Attachments[0].ReferenceLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                break;
            case 9:
                invalid.Attachments[8].ReferenceLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                break;
        }
        EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(invalid, &status, Native::Unexpected), VK_NULL_HANDLE) << fault;
        EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED) << fault;
    }
    device->PhysicalDeviceProperties.properties.limits.maxColorAttachments = 4;
    EXPECT_EQ(cache.TryGetOrCreateCompatibilityRenderPass(key, &status, Native::Unexpected), VK_NULL_HANDLE);
    EXPECT_EQ(status, VK_ERROR_INITIALIZATION_FAILED);
    device->LogicalDevice = VK_NULL_HANDLE;
}

TEST(PSOCacheAdmissionTest, CheckedCompatibilityExtractionRejectsMalformedRecipesWithoutChangingOutput)
{
    PSOCacheTestStorage storage                   = {};
    auto                device                    = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->PhysicalDeviceSupportDynamicRendering = true;
    for (uint32_t fault = 0; fault < 9; ++fault)
    {
        AttachmentSpecification specification = {};
        specification.ColorsMap.init(&storage.Manager.MainArena, 16);
        for (uint32_t i = 0; i < 8; ++i)
        {
            specification.ColorsMap.insert(i, {.Format = ImageFormat::R8G8B8A8_UNORM, .ReferenceLayout = ImageLayout::COLOR_ATTACHMENT_OPTIMAL});
        }
        specification.ColorsMap.insert(8, {.Format = ImageFormat::DEPTH16_UNORM, .ReferenceLayout = ImageLayout::DEPTH_STENCIL_READ_ONLY_OPTIMAL});
        switch (fault)
        {
            case 1:
                specification.ColorsMap.insert(9, {.Format = ImageFormat::R8G8B8A8_UNORM, .ReferenceLayout = ImageLayout::COLOR_ATTACHMENT_OPTIMAL});
                break;
            case 2:
                specification.ColorsMap.find(0)->Format = static_cast<ImageFormat>(UINT32_MAX);
                break;
            case 3:
                specification.ColorsMap.find(0)->ReferenceLayout = static_cast<ImageLayout>(UINT32_MAX);
                break;
            case 4:
                *specification.ColorsMap.find(0) = specification.ColorsMap.at(8);
                break;
            case 5:
                specification.ColorsMap.find(0)->Format = ImageFormat::DEPTH_STENCIL_FROM_DEVICE;
                break;
            case 6:
                specification.ColorsMap.find(0)->ReferenceLayout = ImageLayout::UNDEFINED;
                break;
            case 7:
                specification.BindPoint = ZEngine::Rendering::Specifications::PipelineBindPoint::COMPUTE;
                break;
            case 8:
                specification.ColorsMap.remove(0);
                specification.ColorsMap.insert(9, {});
                break;
        }
        VkResult                      status = VK_SUCCESS;
        Attachment                    attachment(device.get(), std::move(specification), &status);
        PSOCompatibilityRenderPassKey output   = {.ViewMask = 17};
        const auto                    original = output;
        if (fault == 0)
        {
            ASSERT_TRUE(storage.Get().TryMakeCompatibilityRenderPassKey(attachment, &output));
            EXPECT_EQ(output.AttachmentCount, 9u);
            EXPECT_EQ(output.ColorAttachmentCount, 8u);
            EXPECT_EQ(output.DepthAttachmentIndex, 8u);
            EXPECT_EQ(output.Attachments[8].ReferenceLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }
        else
        {
            EXPECT_FALSE(storage.Get().TryMakeCompatibilityRenderPassKey(attachment, &output)) << fault;
            EXPECT_EQ(output, original) << fault;
        }
        EXPECT_FALSE(storage.Get().TryMakeCompatibilityRenderPassKey(attachment, nullptr));
    }
}

class PSOCachePreparationTest : public ::testing::TestWithParam<bool>
{
protected:
    PSOCacheTestStorage                               Storage          = {};
    std::unique_ptr<ZEngine::Hardwares::VulkanDevice> Device           = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    VkPipelineShaderStageCreateInfo                   Stage            = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    VkComputePipelineCreateInfo                       Compute          = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    VkPipelineVertexInputStateCreateInfo              Vertex           = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo            Assembly         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineViewportStateCreateInfo                 Viewport         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo            Rasterization    = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo              Multisample      = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineColorBlendStateCreateInfo               Blend            = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState                                    DynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo                  Dynamic          = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = DynamicStates};
    VkPipelineRenderingCreateInfo                     Rendering        = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    VkGraphicsPipelineCreateInfo                      Graphics         = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};

    void                                              SetUp() override
    {
        PSOCacheTestAccess::SetDevice(Storage.Get(), Device.get());
        PSOCacheTestAccess::EnablePreparation(Storage.Get());
        Stage.stage                  = GetParam() ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_COMPUTE_BIT;
        Stage.module                 = reinterpret_cast<VkShaderModule>(uintptr_t(1));
        Stage.pName                  = "main";
        Compute.stage                = Stage;
        Compute.layout               = reinterpret_cast<VkPipelineLayout>(uintptr_t(2));
        Compute.basePipelineIndex    = -1;
        Graphics.pNext               = &Rendering;
        Graphics.stageCount          = 1;
        Graphics.pStages             = &Stage;
        Graphics.pVertexInputState   = &Vertex;
        Graphics.pInputAssemblyState = &Assembly;
        Graphics.pViewportState      = &Viewport;
        Graphics.pRasterizationState = &Rasterization;
        Graphics.pMultisampleState   = &Multisample;
        Graphics.pColorBlendState    = &Blend;
        Graphics.pDynamicState       = &Dynamic;
        Graphics.layout              = Compute.layout;
        Graphics.basePipelineIndex   = -1;
    }

    PSOPreparationResult Poll(uint32_t generation = 7)
    {
        return GetParam() ? Storage.Get().PrepareGraphicsPipelineAsync(Graphics, generation) : Storage.Get().PrepareComputePipelineAsync(Compute, generation);
    }

    void RequestCallback(void* context, PSOPipelineReadyFn callback)
    {
        if (GetParam())
        {
            (void) Storage.Get().RequestGraphicsPipelineAsync(Graphics, 7, context, callback);
        }
        else
        {
            (void) Storage.Get().RequestComputePipelineAsync(Compute, 7, context, callback);
        }
    }
};

TEST_P(PSOCachePreparationTest, PollsCoalescedImmutableJobWithoutRegisteringCallbacks)
{
    for (uint32_t i = 0; i < 32; ++i)
    {
        const auto result = Poll();
        EXPECT_EQ(result.State, PSOPreparationState::Pending);
        EXPECT_EQ(result.Handle, VK_NULL_HANDLE);
    }
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 1u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineRequests, 1u);
    EXPECT_EQ(PSOCacheTestAccess::QueuedWaiters(Storage.Get()), 0u);
}

TEST_P(PSOCachePreparationTest, FailedCompilationIsTerminalRatherThanSilentlyRescheduled)
{
    ASSERT_EQ(Poll().State, PSOPreparationState::Pending);
    PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_ERROR_OUT_OF_DEVICE_MEMORY);
    for (uint32_t i = 0; i < 32; ++i)
    {
        const auto result = Poll();
        EXPECT_EQ(result.State, PSOPreparationState::Failed);
        EXPECT_EQ(result.Handle, VK_NULL_HANDLE);
        EXPECT_EQ(result.Error, VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 1u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineFailures, 1u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineStaleCompletions, 0u);
    EXPECT_EQ(Poll(8).State, PSOPreparationState::Pending);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 2u);
}

TEST_P(PSOCachePreparationTest, FailedEntryNotifiesLaterCallbackImmediately)
{
    ASSERT_EQ(Poll().State, PSOPreparationState::Pending);
    PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_ERROR_DEVICE_LOST);
    uint32_t notifications = 0;
    RequestCallback(&notifications, [](void* context, VkPipeline pipeline) {
        EXPECT_EQ(pipeline, VK_NULL_HANDLE);
        ++*static_cast<uint32_t*>(context);
    });
    EXPECT_EQ(notifications, 1u);
    EXPECT_EQ(PSOCacheTestAccess::QueuedWaiters(Storage.Get()), 0u);
}

TEST_P(PSOCachePreparationTest, FailedEntryNotifiesAcceptedWaiterOnceAndExpiresNormally)
{
    uint32_t notifications = 0;
    RequestCallback(&notifications, [](void* context, VkPipeline pipeline) {
        EXPECT_EQ(pipeline, VK_NULL_HANDLE);
        ++*static_cast<uint32_t*>(context);
    });
    EXPECT_EQ(notifications, 0u);
    EXPECT_EQ(PSOCacheTestAccess::QueuedWaiters(Storage.Get()), 1u);
    PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_ERROR_OUT_OF_HOST_MEMORY);
    const auto failed = Poll();
    EXPECT_EQ(failed.State, PSOPreparationState::Failed);
    EXPECT_EQ(failed.Error, VK_ERROR_OUT_OF_HOST_MEMORY);
    EXPECT_EQ(notifications, 1u);
    EXPECT_EQ(PSOCacheTestAccess::QueuedWaiters(Storage.Get()), 0u);
    Storage.Get().EvictUnusedPipelines(UINT64_MAX);
    EXPECT_EQ(Poll().State, PSOPreparationState::Pending);
    EXPECT_EQ(notifications, 1u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 2u);
}

TEST_P(PSOCachePreparationTest, SuccessfulVariantRemainsAvailableWithAdmissionStopped)
{
    ASSERT_EQ(Poll().State, PSOPreparationState::Pending);
    const auto handle = reinterpret_cast<VkPipeline>(uintptr_t(3));
    PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_SUCCESS, handle);
    PSOCacheTestAccess::SetPreparationAvailability(Storage.Get(), false, 0);
    const auto result = Poll();
    EXPECT_EQ(result.State, PSOPreparationState::Ready);
    EXPECT_EQ(result.Handle, handle);
    EXPECT_EQ(Poll(8).State, PSOPreparationState::Unavailable);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelinePublishes, 1u);
    Storage.Get().PinPipeline(handle);
    Storage.Get().EvictUnusedPipelines(UINT64_MAX);
    EXPECT_EQ(Poll().Handle, handle);
    Storage.Get().UnpinPipeline(handle);
}

TEST_P(PSOCachePreparationTest, CancelledCompletionDoesNotPoisonVariant)
{
    ASSERT_EQ(Poll().State, PSOPreparationState::Pending);
    PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_ERROR_UNKNOWN, VK_NULL_HANDLE, true);
    EXPECT_EQ(Poll().State, PSOPreparationState::Pending);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 2u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineFailures, 0u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineStaleCompletions, 1u);
}

TEST_P(PSOCachePreparationTest, JobCapacityReturnsRetryableBackpressure)
{
    PSOCacheTestAccess::SaturateJobs(Storage.Get());
    EXPECT_EQ(Poll().State, PSOPreparationState::Backpressure);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 0u);
    PSOCacheTestAccess::ReleaseJobs(Storage.Get());
    EXPECT_EQ(Poll().State, PSOPreparationState::Pending);
}

TEST_P(PSOCachePreparationTest, CollisionBucketCapacityReturnsBackpressureWithoutAsserting)
{
    PSOCacheTestAccess::SaturateBuckets(Storage.Get());
    EXPECT_EQ(Poll().State, PSOPreparationState::Backpressure);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 0u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineBucketOverflows, 1u);
}

TEST_P(PSOCachePreparationTest, UnavailableWorkerServiceDoesNotCompileSynchronously)
{
    PSOCacheTestAccess::SetPreparationAvailability(Storage.Get(), false, 1);
    EXPECT_EQ(Poll().State, PSOPreparationState::Unavailable);
    PSOCacheTestAccess::SetPreparationAvailability(Storage.Get(), true, 0);
    EXPECT_EQ(Poll().State, PSOPreparationState::Unavailable);
    PSOCacheTestAccess::SetDevice(Storage.Get(), nullptr);
    EXPECT_EQ(Poll().State, PSOPreparationState::Unavailable);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 0u);
}

INSTANTIATE_TEST_SUITE_P(ComputeAndGraphics, PSOCachePreparationTest, ::testing::Bool());

TEST_P(PSOCachePreparationTest, WarmupRecordingHashesOncePerExactVariantAndShaderGeneration)
{
    VFSMemoryBackend        backend = {};
    WarmupAllocationContext context = {};
    backend.Initialize(&Storage.Manager.MainArena);
    context.Initialize(&Storage.Manager.MainArena, 4);
    ASSERT_TRUE(context.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    ASSERT_TRUE(context.CreateDir(VFSPath::Parse("/ZodiacEngine/Shaders/Cache").Value()).Succeeded());
    const auto path = VFSPath::Parse(GetParam() ? "/ZodiacEngine/Shaders/Cache/warmup_once_vertex.spv" : "/ZodiacEngine/Shaders/Cache/warmup_once_compute.spv").Value();
    auto       file = context.Open(path, VFSOpenFlags::Write | VFSOpenFlags::Create);
    ASSERT_TRUE(file.Succeeded());
    const uint8_t bytes[] = {1, 2, 3, 4};
    ASSERT_TRUE(file.Value()->Write({bytes, sizeof(bytes)}, 0).Succeeded());
    context.Close(file.Value());
    PSOCacheTestAccess::SetWarmupVFS(Storage.Get(), &context);
    const auto record = [&](uint32_t generation) {
        if (GetParam())
        {
            Storage.Get().RecordGraphicsWarmup("warmup_once", Storage.Get().MakeGraphicsPipelineKey(Graphics, generation), {});
        }
        else
        {
            Storage.Get().RecordComputeWarmup("warmup_once", Storage.Get().MakeComputePipelineKey(Compute, generation));
        }
    };
    const auto prepare = [&](uint32_t generation, uintptr_t handle) {
        ASSERT_EQ(Poll(generation).State, PSOPreparationState::Pending);
        PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_SUCCESS, reinterpret_cast<VkPipeline>(handle));
        ASSERT_EQ(Poll(generation).State, PSOPreparationState::Ready);
    };
    prepare(7, 20);
    for (uint32_t index = 0; index < 100; ++index)
    {
        record(7);
    }
    EXPECT_EQ(context.ShaderReadOpens, 1u);
    EXPECT_EQ(PSOCacheTestAccess::WarmupRecipeCount(Storage.Get()), 1u);
    prepare(8, 21);
    record(8);
    EXPECT_EQ(context.ShaderReadOpens, 2u); // A new generation must hash again.
    Compute.layout = Graphics.layout = reinterpret_cast<VkPipelineLayout>(uintptr_t(3));
    prepare(8, 22);
    record(8);
    EXPECT_EQ(context.ShaderReadOpens, 3u); // A different exact variant is not skipped.
    record(8);
    EXPECT_EQ(context.ShaderReadOpens, 3u);
    PSOCacheTestAccess::SetWarmupVFS(Storage.Get(), nullptr);
    context.Shutdown();
}

class RendererPipelinePreparationTest : public PSOCachePreparationTest
{
protected:
    ZEngine::Rendering::Shaders::Shader                                      Program         = {};
    ComputePipeline                                                          ComputeProgram  = {};
    GraphicPipeline                                                          GraphicsProgram = {};
    GizmoPass                                                                SceneGraphics   = {};
    FrustumCullingPass                                                       SceneCompute    = {};
    std::unique_ptr<ZEngine::Rendering::Renderers::RenderPasses::Attachment> Target          = {};

    IPipeline&                                                               Pipeline()
    {
        return GetParam() ? static_cast<IPipeline&>(GraphicsProgram) : static_cast<IPipeline&>(ComputeProgram);
    }

    void SetUp() override
    {
        PSOCachePreparationTest::SetUp();
        Device->Arena                                 = &Storage.Manager.MainArena;
        Device->PipelineStateCache                    = &Storage.Get();
        Device->PhysicalDeviceSupportDynamicRendering = true;
        PSOCacheTestAccess::SeedEmptyPipelineLayout(Storage.Get());
        Program.ShaderCreateInfos.init(Device->Arena, 1);
        Program.ShaderCreateInfos.push(Stage);
        Program.SetLayouts.init(Device->Arena, 1);
        Program.PushConstants.init(Device->Arena, 1);
        Program.Generation                                                     = 7;
        ZEngine::Rendering::Specifications::AttachmentSpecification attachment = {};
        attachment.ColorsMap.init(Device->Arena, 4);
        attachment.ColorsMap.insert(0, {.Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM});
        Target                     = std::make_unique<ZEngine::Rendering::Renderers::RenderPasses::Attachment>(Device.get(), std::move(attachment));
        GraphicsProgram.Attachment = Target.get();
        Pipeline().Device          = Device.get();
        Pipeline().Shader          = &Program;
    }

    void TearDown() override
    {
        Pipeline().Dispose();
    }

    void Complete(VkPipeline handle)
    {
        PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_SUCCESS, handle);
    }

    void InitializeSceneGraph(RenderGraph& graph)
    {
        graph.Device = Device.get();
        graph.PersistentPasses.init(Device->Arena, RenderGraph::MaxScenePipelineVariants + 1);
        graph.AddCallbackPass("required", GetParam() ? static_cast<IRenderGraphCallbackPass*>(&SceneGraphics) : static_cast<IRenderGraphCallbackPass*>(&SceneCompute), {.Usage = RGScenePipelineUsage::Core, .Formats = {ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM}, .AttachmentCount = GetParam() ? 1u : 0u});
        graph.PersistentPasses[0].PreparedScenePipeline = &Pipeline();
    }
};

TEST_P(RendererPipelinePreparationTest, SceneVariantSetPollsInBoundedSlicesAndExcludesOtherSkyModes)
{
    RenderGraph graph = {};
    InitializeSceneGraph(graph);
    graph.AddCallbackPass("second required", graph.PersistentPasses[0].Callback, graph.PersistentPasses[0].ScenePipelineProfile);
    graph.PersistentPasses[1].PreparedScenePipeline = &Pipeline();
    graph.AddCallbackPass("unselected sky", graph.PersistentPasses[0].Callback, {.Usage = RGScenePipelineUsage::HDRI});
    ASSERT_TRUE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    EXPECT_FALSE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::HDRI));
    EXPECT_EQ(graph.PollScenePipelinePreparation(0), RGScenePipelinePreparationState::Preparing);
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Preparing);
    EXPECT_EQ(graph.PersistentPasses[0].ScenePipelineState, PSOPreparationState::Pending);
    EXPECT_EQ(graph.PersistentPasses[1].ScenePipelineState, PSOPreparationState::Unavailable);
    Complete(reinterpret_cast<VkPipeline>(uintptr_t(3)));
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Preparing);
    EXPECT_EQ(graph.PersistentPasses[1].ScenePipelineState, PSOPreparationState::Ready);
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Ready);
    EXPECT_EQ(graph.PersistentPasses[2].PreparedScenePipeline, nullptr);
    EXPECT_TRUE(Program.HasGenerationLeases());
    graph.EndScenePipelinePreparation();
    EXPECT_FALSE(Program.HasGenerationLeases());
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), Pipeline().Handle), 1u);
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Idle);
}

TEST_P(RendererPipelinePreparationTest, SceneVariantBackpressureIsRetryableAndFailureIsTerminal)
{
    RenderGraph graph = {};
    InitializeSceneGraph(graph);
    ASSERT_TRUE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    PSOCacheTestAccess::SaturateJobs(Storage.Get());
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Preparing);
    EXPECT_EQ(graph.PersistentPasses[0].ScenePipelineState, PSOPreparationState::Backpressure);
    PSOCacheTestAccess::ReleaseJobs(Storage.Get());
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Preparing);
    PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_ERROR_OUT_OF_DEVICE_MEMORY);
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Failed);
    const auto submitted = Storage.Get().GetTelemetry().AsyncPipelineJobs;
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Failed);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, submitted);
    graph.EndScenePipelinePreparation();
    EXPECT_FALSE(Program.HasGenerationLeases());
}

TEST_P(RendererPipelinePreparationTest, CancelledSceneVariantSetReleasesGenerationWhileImmutableJobDrains)
{
    RenderGraph graph = {};
    InitializeSceneGraph(graph);
    ASSERT_TRUE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Preparing);
    graph.EndScenePipelinePreparation();
    EXPECT_FALSE(Program.HasGenerationLeases());
    Complete(reinterpret_cast<VkPipeline>(uintptr_t(3)));
    const auto arena_before = Device->Arena->m_current_offset;
    ASSERT_TRUE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    EXPECT_EQ(graph.PollScenePipelinePreparation(), RGScenePipelinePreparationState::Ready);
    EXPECT_EQ(Device->Arena->m_current_offset, arena_before);
    graph.EndScenePipelinePreparation();
}

TEST_P(RendererPipelinePreparationTest, SceneVariantAdmissionRejectsEmptyMalformedOrUnboundedProfiles)
{
    RenderGraph graph = {};
    InitializeSceneGraph(graph);
    EXPECT_FALSE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::None));
    EXPECT_FALSE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::HDRI));
    const auto valid                                               = graph.PersistentPasses[0].ScenePipelineProfile;
    graph.PersistentPasses[0].ScenePipelineProfile.AttachmentCount = RGScenePipelineProfile::MaxAttachments + 1;
    EXPECT_FALSE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    graph.PersistentPasses[0].ScenePipelineProfile                           = valid;
    graph.PersistentPasses[0].ScenePipelineProfile.AttachmentTargets[0].Load = static_cast<ZEngine::Rendering::Specifications::LoadOperation>(UINT32_MAX);
    if (GetParam())
    {
        EXPECT_FALSE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    }
    graph.PersistentPasses[0].ScenePipelineProfile                                = valid;
    graph.PersistentPasses[0].ScenePipelineProfile.AttachmentTargets[0].DepthRead = true;
    if (GetParam())
    {
        EXPECT_FALSE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    }
    graph.PersistentPasses[0].ScenePipelineProfile = valid;
    for (uint32_t index = 0; index < RenderGraph::MaxScenePipelineVariants; ++index)
    {
        graph.AddCallbackPass("another", graph.PersistentPasses[0].Callback, valid);
    }
    EXPECT_FALSE(graph.BeginScenePipelinePreparation(RGScenePipelineUsage::Core));
    EXPECT_FALSE(Program.HasGenerationLeases());
}

TEST_P(RendererPipelinePreparationTest, BoundedMetadataRefusalPreservesOutgoingAndRetriesWithoutRenderScratch)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    const auto outgoing = reinterpret_cast<VkPipeline>(uintptr_t(3));
    Complete(outgoing);
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    pipeline.ReleasePreparationGeneration();
    ++Program.Generation;
    Program.SetLayouts.reserve(kMaxPSOSetLayouts + 1);
    for (uint32_t i = 0; i <= kMaxPSOSetLayouts; ++i)
    {
        Program.SetLayouts.push(reinterpret_cast<VkDescriptorSetLayout>(uintptr_t(i + 1)));
    }
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
    EXPECT_EQ(pipeline.Handle, outgoing);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), outgoing), 1u);
    Program.SetLayouts.clear();
    if (GetParam())
    {
        auto& bindings   = GraphicsProgram.Description.VertexInputBindingSpecifications;
        auto& attributes = GraphicsProgram.Description.VertexInputAttributeSpecifications;
        bindings.init(Device->Arena, kMaxPSOVertexBindings + 1);
        attributes.init(Device->Arena, kMaxPSOVertexAttributes + 1);
        for (uint32_t i = 0; i <= kMaxPSOVertexBindings; ++i)
        {
            bindings.push({.Stride = 16, .Binding = i});
        }
        EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
        EXPECT_EQ(pipeline.Handle, outgoing);
        bindings.pop();
        for (uint32_t i = 0; i <= kMaxPSOVertexAttributes; ++i)
        {
            attributes.push({.Location = i, .Binding = i % kMaxPSOVertexBindings, .Format = ZEngine::Rendering::Specifications::ImageFormat::R32G32B32_SFLOAT});
        }
        EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
        EXPECT_EQ(pipeline.Handle, outgoing);
        attributes.pop();
    }
    // Pipeline-state assembly must not allocate from, or rewind, render scratch.
    auto*      arena  = Device->Arena;
    const auto before = arena->m_current_offset;
    Device->Arena     = nullptr;
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    EXPECT_EQ(arena->m_current_offset, before);
    const auto replacement = reinterpret_cast<VkPipeline>(uintptr_t(4));
    Complete(replacement);
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    Device->Arena = arena;
    EXPECT_EQ(pipeline.Handle, replacement);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), outgoing), 0u);
}

TEST_P(RendererPipelinePreparationTest, PendingAndFailedReplacementPreserveOutgoingBorrow)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    const auto outgoing = reinterpret_cast<VkPipeline>(uintptr_t(3));
    Complete(outgoing);
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    ASSERT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), outgoing), 1u);
    pipeline.ReleasePreparationGeneration();
    ++Program.Generation;
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    EXPECT_EQ(pipeline.Handle, outgoing);
    EXPECT_EQ(pipeline.BakedShaderGeneration, 7u);
    PSOCacheTestAccess::ResolvePreparation(Storage.Get(), VK_ERROR_OUT_OF_DEVICE_MEMORY);
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
    EXPECT_EQ(pipeline.Handle, outgoing);
    EXPECT_EQ(pipeline.BakedShaderGeneration, 7u);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), outgoing), 1u);
}

TEST_P(RendererPipelinePreparationTest, AdoptsExactGenerationAndReplaysDescriptorsOnce)
{
    auto&    pipeline                = Pipeline();
    uint32_t replays                 = 0;
    pipeline.DescriptorReplayContext = &replays;
    pipeline.ReplayDescriptors       = [](void* context) {
        ++*static_cast<uint32_t*>(context);
        return true;
    };
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    EXPECT_EQ(replays, 0u);
    const auto outgoing = reinterpret_cast<VkPipeline>(uintptr_t(3));
    Complete(outgoing);
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    EXPECT_EQ(replays, 1u);
    pipeline.ReleasePreparationGeneration();
    ++Program.Generation;
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    const auto candidate = reinterpret_cast<VkPipeline>(uintptr_t(4));
    Complete(candidate);
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    EXPECT_EQ(pipeline.Handle, candidate);
    EXPECT_EQ(pipeline.BakedShaderGeneration, 8u);
    EXPECT_EQ(replays, 2u);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), outgoing), 0u);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), candidate), 1u);
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    EXPECT_EQ(replays, 2u);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), candidate), 1u);
    pipeline.Dispose();
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), candidate), 0u);
}

TEST_P(RendererPipelinePreparationTest, DisposalDoesNotLeaveCallbackIntoDestroyedPipeline)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    pipeline.Dispose();
    const auto candidate = reinterpret_cast<VkPipeline>(uintptr_t(3));
    Complete(candidate);
    EXPECT_EQ(pipeline.Handle, VK_NULL_HANDLE);
    EXPECT_EQ(PSOCacheTestAccess::QueuedWaiters(Storage.Get()), 0u);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), candidate), 0u);
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
}

TEST_P(RendererPipelinePreparationTest, DescriptorReplayFailureCannotReportReady)
{
    auto&    pipeline                = Pipeline();
    uint32_t replays                 = 0;
    pipeline.DescriptorReplayContext = &replays;
    pipeline.ReplayDescriptors       = [](void* context) {
        ++*static_cast<uint32_t*>(context);
        return false;
    };
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    Complete(reinterpret_cast<VkPipeline>(uintptr_t(3)));
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
    EXPECT_EQ(replays, 1u);
    EXPECT_FALSE(pipeline.EnsureCurrent());
}

TEST_P(RendererPipelinePreparationTest, PreparationAndSynchronousBakeShareIdenticalCanonicalState)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    const auto candidate = reinterpret_cast<VkPipeline>(uintptr_t(3));
    Complete(candidate);
    pipeline.Bake();
    EXPECT_EQ(pipeline.Handle, candidate);
    EXPECT_EQ(pipeline.BakedShaderGeneration, 7u);
    EXPECT_EQ(PSOCacheTestAccess::PipelinePins(Storage.Get(), candidate), 1u);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 1u);
    EXPECT_TRUE(pipeline.EnsureCurrent());
}

TEST_P(RendererPipelinePreparationTest, VariantChangeIsNotMistakenForReadyShaderGeneration)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    const auto outgoing = reinterpret_cast<VkPipeline>(uintptr_t(3));
    Complete(outgoing);
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    const VkSpecializationMapEntry mapping        = {.constantID = 0, .offset = 0, .size = sizeof(uint32_t)};
    const uint32_t                 value          = 9;
    const VkSpecializationInfo     specialization = {.mapEntryCount = 1, .pMapEntries = &mapping, .dataSize = sizeof(value), .pData = &value};
    if (GetParam())
    {
        GraphicsProgram.Description.EnableBlending = true;
    }
    else
    {
        Program.ShaderCreateInfos[0].pSpecializationInfo = &specialization;
    }
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    EXPECT_EQ(pipeline.Handle, outgoing);
    EXPECT_EQ(pipeline.BakedShaderGeneration, Program.Generation);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 2u);
}

TEST_P(RendererPipelinePreparationTest, PreparationKeepsShaderAndDescriptorGenerationStableUntilExplicitRelease)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    const auto* stages = Program.ShaderCreateInfos.data();
    Program.Reload();
    Program.Reload();
    EXPECT_TRUE(Program.HasDeferredReload());
    EXPECT_EQ(Program.Generation, 7u);
    EXPECT_EQ(Program.ShaderCreateInfos.data(), stages);
    EXPECT_FALSE(Program.FlushDeferredReload());
    Complete(reinterpret_cast<VkPipeline>(uintptr_t(3)));
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Ready);
    EXPECT_FALSE(Program.FlushDeferredReload());
    pipeline.ReleasePreparationGeneration();
    EXPECT_TRUE(Program.HasDeferredReload()); // Release never reloads inline.
    // This CPU-only fixture cannot create a replacement shader. Exhaustion is
    // the real non-Vulkan terminal path and also proves the released pin drains.
    Program.Generation = UINT32_MAX;
    EXPECT_FALSE(Program.FlushDeferredReload());
    EXPECT_FALSE(Program.HasDeferredReload());
    EXPECT_EQ(Program.Generation, UINT32_MAX);
}

TEST_P(RendererPipelinePreparationTest, MultiplePreparationOwnersPreventPrematureReload)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    auto other = Program.RetainGeneration();
    ASSERT_TRUE(other);
    EXPECT_EQ(other.GetGeneration(), 7u);
    EXPECT_EQ(other.GetShader(), &Program);
    Program.Reload();
    pipeline.Dispose();
    EXPECT_FALSE(Program.FlushDeferredReload());
    auto moved = std::move(other);
    EXPECT_FALSE(other);
    EXPECT_TRUE(moved);
    EXPECT_FALSE(Program.FlushDeferredReload());
    other = std::move(moved);
    EXPECT_FALSE(moved);
    EXPECT_TRUE(other);
    other.Reset();
    other.Reset();
    Program.Generation = UINT32_MAX;
    EXPECT_FALSE(Program.FlushDeferredReload());
    EXPECT_FALSE(Program.HasDeferredReload());
}

TEST_P(RendererPipelinePreparationTest, UnexpectedGenerationMutationCannotSilentlyChangePreparationIdentity)
{
    auto& pipeline = Pipeline();
    ASSERT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Pending);
    ++Program.Generation; // Simulate a caller bypassing the shader lifecycle API.
    EXPECT_EQ(pipeline.PrepareAsync(), PSOPreparationState::Failed);
    EXPECT_EQ(Storage.Get().GetTelemetry().AsyncPipelineJobs, 1u);
    EXPECT_EQ(pipeline.Handle, VK_NULL_HANDLE);
}

TEST_P(RendererPipelinePreparationTest, DeviceSafePointRetainsDeferredReloadWithoutAnotherFileEvent)
{
    Device->ShaderManager.Initialize(Device->Arena, 1);
    Device->ShaderCaches.init(Device->Arena, 4);
    const auto handle = Device->ShaderManager.Create();
    ASSERT_TRUE(handle);
    auto* shader = Device->ShaderManager.Access(handle);
    ASSERT_NE(shader, nullptr);
    shader->ShaderCreateInfos.init(Device->Arena, 1);
    shader->ShaderCreateInfos.push(Stage);
    shader->m_specification.ComputeFilename = "/scene-preparation/shader.spv";
    shader->Generation                      = 12;
    Device->ShaderCaches.insert("prepared", handle);
    auto lease = shader->RetainGeneration();
    ASSERT_TRUE(lease);
    Device->RequestShaderReload("/scene-preparation/shader.spv");
    Device->FlushShaderReloadRequests();
    EXPECT_TRUE(shader->HasDeferredReload());
    EXPECT_EQ(shader->Generation, 12u);
    Device->FlushShaderReloadRequests();
    EXPECT_TRUE(shader->HasDeferredReload());
    EXPECT_EQ(shader->Generation, 12u);
    lease.Reset();
    shader->Generation = UINT32_MAX; // Exercise the non-Vulkan exhausted drain.
    Device->FlushShaderReloadRequests();
    EXPECT_FALSE(shader->HasDeferredReload());
    EXPECT_EQ(shader->Generation, UINT32_MAX);
}

TEST(ShaderGenerationLeaseTest, EmptyProgramCannotProduceAReadinessLease)
{
    ZEngine::Rendering::Shaders::Shader shader = {};
    EXPECT_FALSE(shader.RetainGeneration());
}

INSTANTIATE_TEST_SUITE_P(ComputeAndGraphics, RendererPipelinePreparationTest, ::testing::Bool());

class PSOCacheWarmupTest : public ::testing::Test
{
protected:
    MemoryManager                     Memory      = {};
    ArenaAllocator                    DeviceArena = {};
    VFSMemoryBackend                  Backend     = {};
    WarmupAllocationContext           Context     = {};
    ZEngine::Hardwares::VulkanDevice* Device      = nullptr;
    PSOCache*                         Cache       = nullptr;

    void                              SetUp() override
    {
        Memory.Initialize(ZMega(64), {});
        Memory.MainArena.CreateSubArena(ZMega(4), &DeviceArena, "PSOCacheTest/Device");
        Device        = ZPushStructCtor<ZEngine::Hardwares::VulkanDevice>(&Memory.MainArena);
        Device->Arena = &DeviceArena;
        Cache         = ZPushStructCtor<PSOCache>(&Memory.MainArena);
        PSOCacheTestAccess::SetDevice(*Cache, Device);
        Backend.Initialize(&Memory.MainArena);
        Context.Initialize(&Memory.MainArena, 4);
        ASSERT_TRUE(Context.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
        Context.DeviceArena = &DeviceArena;
    }

    void TearDown() override
    {
        Context.Shutdown();
    }

    void SaveRecipeWithMissingShader()
    {
        PSOWarmupRecipe recipe   = {};
        recipe.ShaderContentHash = 1;
        ZEngine::Helpers::secure_strcpy(recipe.ShaderName, sizeof(recipe.ShaderName), "missing_warmup_shader");
        PSOCacheTestAccess::AddWarmupRecipe(*Cache, recipe);
        ASSERT_TRUE(Cache->SaveWarmupRecipes(Context));
    }
};

TEST_F(PSOCacheWarmupTest, PreservesPersistentAllocationsMadeWhileResolvingRecipes)
{
    ASSERT_NO_FATAL_FAILURE(SaveRecipeWithMissingShader());
    ASSERT_TRUE(Cache->LoadWarmupRecipes(Context));
    ASSERT_NE(Context.PersistentValue, nullptr);
    EXPECT_EQ(DeviceArena.m_current_offset, Context.ExpectedCurrentOffset);
    EXPECT_EQ(DeviceArena.m_previous_offset, Context.ExpectedPreviousOffset);

    void* unrelated = DeviceArena.Allocate(ZKilo(64));
    ASSERT_NE(unrelated, nullptr);
    EXPECT_EQ(ZEngine::Helpers::secure_memset(unrelated, 0xFF, ZKilo(64), ZKilo(64)), ZEngine::Helpers::MEMORY_OP_SUCCESS);
    EXPECT_EQ(*Context.PersistentValue, WarmupAllocationContext::kPersistentValue);
    EXPECT_EQ(PSOCacheTestAccess::WarmupRecipeCount(*Cache), 0u);
    EXPECT_EQ(Cache->GetTelemetry().WarmupRecipeSkips, 1u);
}

TEST_F(PSOCacheWarmupTest, RepeatedLoadsDoNotConsumeDeviceArenaStorage)
{
    ASSERT_NO_FATAL_FAILURE(SaveRecipeWithMissingShader());
    ASSERT_TRUE(Cache->LoadWarmupRecipes(Context));
    ASSERT_NE(Context.PersistentValue, nullptr);

    for (uint32_t iteration = 0; iteration < 8; ++iteration)
    {
        ASSERT_TRUE(Cache->LoadWarmupRecipes(Context));
        EXPECT_EQ(DeviceArena.m_current_offset, Context.ExpectedCurrentOffset);
        EXPECT_EQ(DeviceArena.m_previous_offset, Context.ExpectedPreviousOffset);
        EXPECT_EQ(*Context.PersistentValue, WarmupAllocationContext::kPersistentValue);
    }
    EXPECT_EQ(Cache->GetTelemetry().WarmupRecipeLoadSuccesses, 9u);
}

TEST_F(PSOCacheWarmupTest, LoadsEmptyRecipeFileWithoutDeviceArenaAllocations)
{
    ASSERT_TRUE(Cache->SaveWarmupRecipes(Context));
    const size_t current_offset  = DeviceArena.m_current_offset;
    const size_t previous_offset = DeviceArena.m_previous_offset;
    EXPECT_TRUE(Cache->LoadWarmupRecipes(Context));
    EXPECT_EQ(DeviceArena.m_current_offset, current_offset);
    EXPECT_EQ(DeviceArena.m_previous_offset, previous_offset);
    EXPECT_EQ(Context.PersistentValue, nullptr);
    EXPECT_EQ(PSOCacheTestAccess::WarmupRecipeCount(*Cache), 0u);
}

TEST_F(PSOCacheWarmupTest, RejectsCorruptPayloadWithoutResolvingShadersOrDroppingExistingRecipes)
{
    ASSERT_NO_FATAL_FAILURE(SaveRecipeWithMissingShader());
    const auto path = VFSPath::Parse("/cache/pso/vulkan-pipeline-warmup.bin");
    ASSERT_TRUE(path.Succeeded());
    auto opened = Context.Open(path.Value(), VFSOpenFlags::Write);
    ASSERT_TRUE(opened.Succeeded());
    const uint8_t corrupt_byte = 0xFF;
    auto          written      = opened.Value()->Write({&corrupt_byte, 1}, 64);
    Context.Close(opened.Value());
    ASSERT_TRUE(written.Succeeded());
    ASSERT_EQ(written.Value(), 1u);

    const size_t current_offset  = DeviceArena.m_current_offset;
    const size_t previous_offset = DeviceArena.m_previous_offset;
    EXPECT_FALSE(Cache->LoadWarmupRecipes(Context));
    EXPECT_EQ(DeviceArena.m_current_offset, current_offset);
    EXPECT_EQ(DeviceArena.m_previous_offset, previous_offset);
    EXPECT_EQ(Context.PersistentValue, nullptr);
    EXPECT_EQ(PSOCacheTestAccess::WarmupRecipeCount(*Cache), 1u);
}

TEST_F(PSOCacheWarmupTest, RejectsPreviousCompatibilityRecordVersionBeforeResolvingShadersAndCanRegenerate)
{
    ASSERT_NO_FATAL_FAILURE(SaveRecipeWithMissingShader());
    const auto path = VFSPath::Parse("/cache/pso/vulkan-pipeline-warmup.bin");
    ASSERT_TRUE(path.Succeeded());
    auto opened = Context.Open(path.Value(), VFSOpenFlags::Write);
    ASSERT_TRUE(opened.Succeeded());
    const uint8_t previous_version[4] = {3, 0, 0, 0};
    const auto    written             = opened.Value()->Write({previous_version, sizeof(previous_version)}, 4);
    Context.Close(opened.Value());
    ASSERT_TRUE(written.Succeeded());
    ASSERT_EQ(written.Value(), sizeof(previous_version));
    const auto before = DeviceArena.m_current_offset;
    EXPECT_FALSE(Cache->LoadWarmupRecipes(Context));
    EXPECT_EQ(Context.PersistentValue, nullptr);
    EXPECT_EQ(DeviceArena.m_current_offset, before);
    EXPECT_EQ(PSOCacheTestAccess::WarmupRecipeCount(*Cache), 1u);
    ASSERT_TRUE(Cache->SaveWarmupRecipes(Context));
    EXPECT_TRUE(Cache->LoadWarmupRecipes(Context));
    EXPECT_NE(Context.PersistentValue, nullptr);
}

TEST_F(PSOCacheWarmupTest, RejectsShortAndFailedPayloadReadsAndRecovers)
{
    ASSERT_NO_FATAL_FAILURE(SaveRecipeWithMissingShader());
    const size_t current_offset  = DeviceArena.m_current_offset;
    const size_t previous_offset = DeviceArena.m_previous_offset;

    for (auto mode : {WarmupRecipeFile::PayloadReadMode::Short, WarmupRecipeFile::PayloadReadMode::Failure})
    {
        SCOPED_TRACE(static_cast<uint8_t>(mode));
        Context.RecipeFile.ReadMode = mode;
        EXPECT_FALSE(Cache->LoadWarmupRecipes(Context));
        EXPECT_EQ(Context.RecipeFile.File, nullptr);
        EXPECT_EQ(Context.PersistentValue, nullptr);
        EXPECT_EQ(DeviceArena.m_current_offset, current_offset);
        EXPECT_EQ(DeviceArena.m_previous_offset, previous_offset);
        EXPECT_EQ(PSOCacheTestAccess::WarmupRecipeCount(*Cache), 1u);
        EXPECT_EQ(Cache->GetTelemetry().WarmupRecipeLoadSuccesses, 0u);
    }

    Context.RecipeFile.ReadMode = WarmupRecipeFile::PayloadReadMode::Complete;
    EXPECT_TRUE(Cache->LoadWarmupRecipes(Context));
    EXPECT_EQ(Context.RecipeFile.File, nullptr);
    EXPECT_NE(Context.PersistentValue, nullptr);
    EXPECT_EQ(DeviceArena.m_current_offset, Context.ExpectedCurrentOffset);
    EXPECT_EQ(DeviceArena.m_previous_offset, Context.ExpectedPreviousOffset);
    EXPECT_EQ(Cache->GetTelemetry().WarmupRecipeLoadSuccesses, 1u);
}

TEST(PSOCacheAdmissionTest, SaturatedBackgroundPoolReturnsLeaseAndLeavesJobQueued)
{
    SaturatedBackgroundPool background{};
    ASSERT_TRUE(background.Fill());
    auto cache = std::make_unique<PSOCache>();
    PSOCacheTestAccess::CheckSaturatedAsyncDispatch(*cache);
}

TEST(PSOCacheInvalidationTest, RetainsPinnedComputePipeline)
{
    EXPECT_TRUE(PSOCacheTestAccess::RetainsPinnedInvalidatedComputePipeline());
}

TEST(PSOCacheInvalidationTest, RetainsPinnedGraphicsPipeline)
{
    EXPECT_TRUE(PSOCacheTestAccess::RetainsPinnedInvalidatedGraphicsPipeline());
}

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
