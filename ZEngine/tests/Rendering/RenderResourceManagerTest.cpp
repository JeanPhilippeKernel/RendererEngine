#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderHandle.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <gtest/gtest.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <uuid.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <random>
#include <thread>
#include <utility>
#include <vector>
#include "../Misc/SaturatedBackgroundPool.h"

using namespace ZEngine;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Rendering;
using namespace ZEngine::Managers;

struct RRMDecodeTestHelper
{
    static uint32_t FrameMaterialCount(const RenderResourceManager& manager)
    {
        return manager.m_frame_material_count;
    }
    static uint32_t FrameTextureCount(const RenderResourceManager& manager)
    {
        return manager.m_frame_texture_count;
    }
    static void StampResources(RenderResourceManager& manager, uint32_t timeline_index, uint64_t value)
    {
        manager.StampResourceSubmission(timeline_index, value);
    }
    static void CompleteResources(RenderResourceManager& manager, const std::array<uint64_t, RenderResourceManager::ResourceTimelineCount>& completed)
    {
        for (uint32_t index = 0; index < RenderResourceManager::ResourceTimelineCount; ++index)
        {
            manager.m_resource_completed[index] = completed[index];
        }
        manager.CollectFrameResources(manager.m_resource_completed);
    }
    static void ReleaseFrameResources(RenderResourceManager& manager)
    {
        manager.ReleaseFrameResourcesAfterIdle();
    }
    static void InitDecode(RenderResourceManager& manager, ArenaAllocator& arena, Hardwares::VulkanDevice& device)
    {
        manager.m_device = &device;
        manager.m_upload_slabs[0].Init(&arena, RenderResourceManager::UPLOAD_SLAB_BYTES);
        manager.m_texture_task_slab.Init(&arena, RenderResourceManager::TEXTURE_TASK_SLAB_BYTES);
    }

    static bool SubmitDecode(RenderResourceManager& manager, const char* filename, uint32_t extent, bool fit = false, Textures::TextureHandle tracked = {})
    {
        uint8_t index = UINT8_MAX;
        if (!Acquire(manager, &index))
        {
            return false;
        }
        auto* task                = new (manager.m_texture_task_slab.Alloc(sizeof(RenderResourceManager::TextureDecodeTask))) RenderResourceManager::TextureDecodeTask{};
        task->Owner               = &manager;
        task->DecodeSlabIndex     = index;
        task->Specification.Width = task->Specification.Height = extent;
        task->FitIntoSquare                                    = fit;
        task->Texture                                          = tracked;
        task->TrackCompletion                                  = tracked.Valid();
        if (task->TrackCompletion)
        {
            manager.TrackTextureDecode(tracked);
        }
        std::snprintf(task->Filename, sizeof(task->Filename), "%s", filename);
        manager.m_pending_texture_decodes.value.fetch_add(1);
        RenderResourceManager::RunTextureDecodeTask(task);
        return true;
    }

    static uint32_t ActiveDecodeSlabs(const RenderResourceManager& manager)
    {
        return manager.m_active_texture_decode_slabs.value.load(std::memory_order_acquire);
    }

    static bool Decode(RenderResourceManager& manager, const char* filename, uint32_t extent, bool fit = false)
    {
        if (!SubmitDecode(manager, filename, extent, fit))
        {
            return false;
        }
        RenderResourceManager::TextureDeferral deferral = {};
        if (!manager.m_tex_deferral_queue.pop(deferral))
            return false;
        const bool valid = deferral.Pixels && deferral.ByteSize == static_cast<size_t>(extent) * extent * 4;
        deferral.Slab->Free(deferral.Pixels);
        Release(manager, deferral.DecodeSlabIndex);
        return valid;
    }

    static void ShutdownDecode(RenderResourceManager& manager)
    {
        manager.m_upload_slabs[0].Shutdown();
        manager.m_texture_task_slab.Shutdown();
    }

    static bool Acquire(RenderResourceManager& manager, uint8_t* out_index)
    {
        return manager.TryAcquireTextureDecodeSlab(out_index);
    }

    static void Release(RenderResourceManager& manager, uint8_t index)
    {
        manager.ReleaseTextureDecodeSlab(index);
    }

    static constexpr uint32_t Capacity()
    {
        return RenderResourceManager::MAX_CONCURRENT_TEXTURE_DECODES;
    }

    static bool Track(RenderResourceManager& manager, Textures::TextureHandle texture)
    {
        return manager.TrackTextureDecode(texture);
    }

    static void Submitted(RenderResourceManager& manager, Textures::TextureHandle texture)
    {
        manager.MarkTextureUploadSubmitted(texture);
    }

    static void PublishDecode(RenderResourceManager& manager, Textures::TextureHandle texture, bool success)
    {
        manager.PublishTextureDecodeCompletion(texture, success);
    }

    static void ReleaseLater(RenderResourceManager& manager, Textures::TextureHandle texture)
    {
        manager.QueueTextureRelease(texture);
    }

    static void FlushReleases(RenderResourceManager& manager)
    {
        manager.FlushPendingTextureReleases();
    }

    static uint32_t ReleaseCount(const RenderResourceManager& manager)
    {
        return manager.m_pending_texture_release_count;
    }

    static bool Fit(const uint8_t* source, int width, int height, uint8_t* destination, uint32_t extent)
    {
        return RenderResourceManager::AspectFitThumbnail(source, width, height, destination, extent);
    }

    static bool WriteCache(const std::filesystem::path& path, uint8_t* pixels, uint32_t width, uint32_t height, const RenderResourceManager::ThumbnailCacheIdentity& key = {}, bool bottom_up = true)
    {
        return RenderResourceManager::WriteThumbnailCache(path.string().c_str(), pixels, width, height, key, bottom_up);
    }

    static void AcceptWork(RenderResourceManager& manager, bool enabled = true)
    {
        manager.m_accept_texture_decodes.value.store(enabled);
    }
    static uint32_t Pending(const RenderResourceManager& manager)
    {
        return manager.m_pending_texture_decodes.value.load();
    }
    static bool Reserved(void* context)
    {
        return static_cast<RenderResourceManager::ThumbnailCacheWriteTask*>(context)->Reserved;
    }
    static void SimulateDroppedWorker(RenderResourceManager& manager, void* context)
    {
        static_cast<RenderResourceManager::ThumbnailCacheWriteTask*>(context)->WorkerQueued = true;
        manager.m_pending_texture_decodes.value.fetch_add(1);
    }
    static void CancelStoppedWrites(RenderResourceManager& manager)
    {
        manager.CancelStoppedThumbnailCacheWrites();
    }
    static void QueueUnsubmittedUpload(RenderResourceManager& manager)
    {
        manager.m_async_uploads.Enqueue({});
    }
    static void ClearUnsubmittedUploads(RenderResourceManager& manager)
    {
        manager.m_async_uploads.Clear();
    }
    static void SetDevice(RenderResourceManager& manager, Hardwares::VulkanDevice* device)
    {
        manager.m_device = device;
    }
    static RenderResourceManager::MaterialVersionPin RetainMaterial(RenderResourceManager& manager, const Meshes::MeshMaterial& material)
    {
        return manager.RetainMaterialVersion(material);
    }
    static uint64_t VersionPins(const RenderResourceManager& manager)
    {
        return manager.m_texture_version_pin_count;
    }
    static uint32_t SetTextureReferences(RenderResourceManager& manager, Textures::TextureHandle handle, uint32_t references)
    {
        std::lock_guard lock(manager.m_texture_version_mutex);
        return std::exchange(manager.m_texture_versions[handle.Index].References, references);
    }
    static void ExhaustVersionIds(RenderResourceManager& manager)
    {
        manager.m_texture_version_sequence  = UINT64_MAX;
        manager.m_material_version_sequence = UINT64_MAX;
    }
    static void SealVersions(RenderResourceManager& manager)
    {
        manager.m_scene_resources_sealed.store(true);
    }
    static void DiscardDeferrals(RenderResourceManager& manager)
    {
        manager.DiscardTextureDeferrals();
    }
    static RenderResourceManager::TextureDeferral MakeDeferral(RenderResourceManager& manager, Textures::TextureHandle texture)
    {
        uint8_t index = UINT8_MAX;
        if (!Acquire(manager, &index))
        {
            return {};
        }
        auto& slab = manager.m_upload_slabs[index];
        return {.Pixels = static_cast<uint8_t*>(slab.Alloc(4)), .ByteSize = 4, .Slab = &slab, .DecodeSlabIndex = index, .TexHandle = texture};
    }
    static bool ProcessDeferral(RenderResourceManager& manager, RenderResourceManager::TextureDeferral& deferral)
    {
        return manager.ProcessTextureDeferral(0, deferral);
    }
    static void InitDeferralRetry(RenderResourceManager& manager, ArenaAllocator& arena)
    {
        manager.m_tex_deferral_retry.init(&arena, 4);
    }
    static size_t DeferralRetryCount(const RenderResourceManager& manager)
    {
        return manager.m_tex_deferral_retry.size();
    }
    static bool QueueDecode(RenderResourceManager& manager, Textures::TextureHandle texture)
    {
        auto* task            = new (manager.m_texture_task_slab.Alloc(sizeof(RenderResourceManager::TextureDecodeTask))) RenderResourceManager::TextureDecodeTask{};
        task->Owner           = &manager;
        task->Texture         = texture;
        task->TrackCompletion = true;
        if (!manager.m_queued_texture_decodes.push(task))
        {
            manager.m_texture_task_slab.Free(task);
            return false;
        }
        return manager.TrackTextureDecode(texture);
    }
    static void DispatchDecodes(RenderResourceManager& manager)
    {
        manager.DispatchQueuedTextureDecodes();
    }
    static uint32_t FillDeferralQueue(RenderResourceManager& manager)
    {
        uint32_t count = 0;
        while (manager.m_tex_deferral_queue.push({}))
        {
            ++count;
        }
        return count;
    }
    static void AdmitTextureRetirements(RenderResourceManager& manager, bool fill_queue = false, bool clear_queue = false)
    {
        std::lock_guard lock(manager.m_pending_texture_release_mutex);
        if (fill_queue)
        {
            manager.m_pending_texture_release_count = manager.MAX_PENDING;
        }
        if (clear_queue)
        {
            manager.m_pending_texture_release_count = 0;
        }
        manager.AdmitTextureVersionRetirements();
    }
    static bool RetirementWaiting(const RenderResourceManager& manager, uint64_t index)
    {
        return (manager.m_texture_retirement_ready[index / 64] & (uint64_t(1) << (index % 64))) != 0;
    }
    static void SetRegistry(RenderResourceManager& manager, AssetRegistry* registry)
    {
        manager.m_registry = registry;
    }
    static bool StageReplacement(RenderResourceManager& manager, const uuids::uuid& uuid, Textures::TextureHandle expected, Textures::TextureHandle candidate, bool completed = true)
    {
        for (auto& replacement : manager.m_texture_replacements)
        {
            if (!replacement.Replacement.Valid())
            {
                if (!manager.TrackTextureDecode(candidate))
                {
                    return false;
                }
                replacement                  = {};
                replacement.UUID             = uuid;
                replacement.Expected         = expected;
                replacement.Replacement      = candidate;
                replacement.ProducerComplete = completed;
                manager.m_texture_replacement_count.fetch_add(1);
                return true;
            }
        }
        return false;
    }
    static void CompleteReplacement(RenderResourceManager& manager, Textures::TextureHandle candidate, bool success)
    {
        manager.PublishTextureDecodeCompletion(candidate, success);
    }
    static void PollReplacements(RenderResourceManager& manager)
    {
        manager.PollTextureReplacements();
    }
    static void CancelReplacements(RenderResourceManager& manager)
    {
        manager.CancelTextureReplacements();
    }
    static uint32_t ReplacementCount(const RenderResourceManager& manager)
    {
        return manager.m_texture_replacement_count.load();
    }
    static bool ReleaseQueued(const RenderResourceManager& manager, Textures::TextureHandle handle)
    {
        for (uint32_t index = 0; index < manager.m_pending_texture_release_count; ++index)
        {
            const auto pending = manager.m_pending_texture_releases[index];
            if (pending.Index == handle.Index && pending.Generation == handle.Generation)
            {
                return true;
            }
        }
        return false;
    }
    static void SimulateDroppedCleanup(RenderResourceManager& manager)
    {
        manager.m_thumbnail_cache_cleanup.WorkerQueued = true;
        manager.m_thumbnail_maintenance_stats.Pending  = true;
        manager.m_pending_texture_decodes.value.fetch_add(1);
    }

    static bool ReadCache(const std::filesystem::path& path, uint32_t width, uint32_t height, const RenderResourceManager::ThumbnailCacheIdentity& key = {})
    {
        auto*      pixels  = RenderResourceManager::ReadThumbnailCache(path.string().c_str(), width, height, key);
        const bool success = pixels != nullptr;
        stbi_image_free(pixels);
        return success;
    }
};

TEST(TextureDecodeObservation, EveryRetainableTextureSlotCanPublishWithoutACompletionQueue)
{
    auto           manager  = std::make_unique<RenderResourceManager>();
    constexpr auto capacity = RenderResourceManager::TextureVersionCapacity;
    for (uint32_t index = 0; index < capacity; ++index)
    {
        ASSERT_TRUE(RRMDecodeTestHelper::Track(*manager, {index, 0}));
        EXPECT_EQ(manager->GetTextureDecodeState({index, 0}), RenderResourceManager::TextureDecodeState::Pending);
    }
    EXPECT_FALSE(RRMDecodeTestHelper::Track(*manager, {}));
    EXPECT_FALSE(RRMDecodeTestHelper::Track(*manager, {capacity, 0}));
    std::atomic<bool> start   = false;
    const auto        publish = [&](uint32_t first, uint32_t last, bool success) {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        for (uint32_t index = first; index < last; ++index)
        {
            RRMDecodeTestHelper::PublishDecode(*manager, {index, 0}, success);
        }
    };
    std::thread failures(publish, 0, capacity / 2, false);
    std::thread successes(publish, capacity / 2, capacity, true);
    start.store(true, std::memory_order_release);
    for (uint32_t index = 0; index < capacity; ++index)
    {
        const auto state = manager->GetTextureDecodeState({index, 0});
        EXPECT_TRUE(state == RenderResourceManager::TextureDecodeState::Pending || state == RenderResourceManager::TextureDecodeState::Failed || state == RenderResourceManager::TextureDecodeState::Succeeded);
    }
    failures.join();
    successes.join();
    for (uint32_t index = 0; index < capacity; ++index)
    {
        EXPECT_EQ(manager->GetTextureDecodeState({index, 0}), index < capacity / 2 ? RenderResourceManager::TextureDecodeState::Failed : RenderResourceManager::TextureDecodeState::Succeeded);
    }
}

TEST(TextureDecodeObservation, StaleCompletionsAndForgettingCannotChangeAReusedGeneration)
{
    auto                          manager = std::make_unique<RenderResourceManager>();
    const Textures::TextureHandle old     = {0, 0};
    const Textures::TextureHandle current = {0, 2};
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*manager, old));
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*manager, current));
    EXPECT_FALSE(RRMDecodeTestHelper::Track(*manager, old));
    RRMDecodeTestHelper::PublishDecode(*manager, old, false);
    manager->ForgetTextureDecode(old);
    EXPECT_EQ(manager->GetTextureDecodeState(old), RenderResourceManager::TextureDecodeState::Untracked);
    EXPECT_EQ(manager->GetTextureDecodeState(current), RenderResourceManager::TextureDecodeState::Pending);
    RRMDecodeTestHelper::PublishDecode(*manager, current, false);
    RRMDecodeTestHelper::Submitted(*manager, current);
    EXPECT_EQ(manager->GetTextureDecodeState(current), RenderResourceManager::TextureDecodeState::Failed);
    manager->ForgetTextureDecode(current);
    RRMDecodeTestHelper::PublishDecode(*manager, current, true);
    EXPECT_EQ(manager->GetTextureDecodeState(current), RenderResourceManager::TextureDecodeState::Untracked);
    EXPECT_EQ(manager->GetTextureDecodeState({RenderResourceManager::TextureVersionCapacity, 0}), RenderResourceManager::TextureDecodeState::Untracked);
}

static uuids::uuid MakeUUID()
{
    std::random_device           rd;
    std::mt19937                 gen(rd());
    uuids::uuid_random_generator g{gen};
    return g();
}

class ResourceVersionTest : public ::testing::Test
{
protected:
    MemoryManager                            Memory    = {};
    std::unique_ptr<Hardwares::VulkanDevice> Device    = std::make_unique<Hardwares::VulkanDevice>();
    std::unique_ptr<RenderResourceManager>   Manager   = std::make_unique<RenderResourceManager>();
    Hardwares::DeviceSwapchain               Swapchain = {};

    void                                     SetUp() override
    {
        Memory.Initialize(ZMega(4), {});
        Device->Arena            = &Memory.MainArena;
        Device->SwapchainPtr     = &Swapchain;
        Device->MaxGlobalTexture = 8;
        Device->GlobalTextures.Initialize(&Memory.MainArena, Device->MaxGlobalTexture);
        Device->ImageBufferManager.Initialize(&Memory.MainArena, Device->MaxGlobalTexture);
        Device->RRM = Manager.get();
        RRMDecodeTestHelper::SetDevice(*Manager, Device.get());
    }

    void TearDown() override
    {
        EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
        Device->RRM = nullptr;
    }

    Textures::TextureHandle MakeTexture()
    {
        const auto handle  = Device->GlobalTextures.Create();
        auto*      texture = Device->GlobalTextures.Access(handle);
        if (texture)
        {
            texture->Width                                                                       = 16;
            texture->Height                                                                      = 8;
            texture->Specification.Format                                                        = Specifications::ImageFormat::R8G8B8A8_SRGB;
            texture->BufferHandle                                                                = Device->ImageBufferManager.Create();
            Device->ImageBufferManager.Access(texture->BufferHandle)->GetBuffer().OwnsAllocation = true;
        }
        return handle;
    }
};

TEST_F(ResourceVersionTest, FrameTextureTransferPreservesExactLeaseAndRefusesInvalidOwners)
{
    const auto texture = MakeTexture();
    auto       pin     = Manager->PinTextureVersion(texture);
    ASSERT_TRUE(pin);
    const auto                                    version  = pin.Get()->Version;
    RenderResourceManager::TextureVersionSnapshot snapshot = {.Version = 777};
    RenderResourceManager::TextureVersionPin      empty    = {};
    EXPECT_FALSE(Manager->GetFrameTextureVersion(empty, &snapshot));
    EXPECT_EQ(snapshot.Version, 777u);
    auto other = std::make_unique<RenderResourceManager>();
    EXPECT_FALSE(other->GetFrameTextureVersion(pin, &snapshot));
    EXPECT_EQ(snapshot.Version, 777u);
    EXPECT_FALSE(Manager->GetFrameTextureVersion(pin, nullptr));
    EXPECT_EQ(RRMDecodeTestHelper::FrameTextureCount(*Manager), 0u);
    EXPECT_TRUE(Manager->RetainTextureForRetirement(texture));
    EXPECT_FALSE(Manager->PinTextureVersion(texture));
    auto bank_pin = Manager->RetainTextureVersion(pin);
    ASSERT_TRUE(bank_pin);
    ASSERT_TRUE(Manager->GetFrameTextureVersion(bank_pin, &snapshot));
    EXPECT_EQ(snapshot.Version, version);
    ASSERT_TRUE(Manager->GetFrameTextureVersion(pin, &snapshot));
    EXPECT_EQ(RRMDecodeTestHelper::FrameTextureCount(*Manager), 1u);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 3u);
    pin.Reset();
    bank_pin.Reset();
    EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
    Manager->FinishResourceFrame(); // No submission: release the recording hold.
    EXPECT_FALSE(Manager->IsTextureVersionPinned(texture));
    EXPECT_EQ(RRMDecodeTestHelper::FrameTextureCount(*Manager), 0u);
}

TEST_F(ResourceVersionTest, FrameTextureWaitsForEveryAcceptedQueueAndPresentationGate)
{
    const auto texture = MakeTexture();
    auto       pin     = Manager->PinTextureVersion(texture);
    ASSERT_TRUE(pin);
    RenderResourceManager::TextureVersionSnapshot snapshot = {};
    ASSERT_TRUE(Manager->GetFrameTextureVersion(pin, &snapshot));
    pin.Reset();
    std::array<uint64_t, RenderResourceManager::ResourceTimelineCount> completed = {};
    for (uint32_t queue = 0; queue < completed.size(); ++queue)
    {
        RRMDecodeTestHelper::StampResources(*Manager, queue, queue + 3);
    }
    Manager->FinishResourceFrame();
    for (uint32_t queue = 0; queue < completed.size(); ++queue)
    {
        EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
        completed[queue] = queue + 3;
        RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    }
    EXPECT_FALSE(Manager->IsTextureVersionPinned(texture));
    EXPECT_EQ(RRMDecodeTestHelper::FrameTextureCount(*Manager), 0u);
}

TEST_F(ResourceVersionTest, LaterTextureRecordingExtendsEarlierAcceptedWork)
{
    const auto texture = MakeTexture();
    auto       pin     = Manager->PinTextureVersion(texture);
    ASSERT_TRUE(pin);
    RenderResourceManager::TextureVersionSnapshot snapshot = {}, repeated = {};
    ASSERT_TRUE(Manager->GetFrameTextureVersion(pin, &snapshot));
    RRMDecodeTestHelper::StampResources(*Manager, 0, 7);
    Manager->FinishResourceFrame();
    ASSERT_TRUE(Manager->GetFrameTextureVersion(pin, &repeated));
    pin.Reset();
    EXPECT_EQ(snapshot.Version, repeated.Version);
    std::array<uint64_t, RenderResourceManager::ResourceTimelineCount> completed = {};
    completed[0]                                                                 = 7;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
    RRMDecodeTestHelper::StampResources(*Manager, 0, 9);
    Manager->FinishResourceFrame();
    EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
    completed[0] = 9;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_FALSE(Manager->IsTextureVersionPinned(texture));
}

TEST_F(ResourceVersionTest, SparseTextureCollectionKeepsTheMovedPendingEntry)
{
    std::array<Textures::TextureHandle, 8> textures = {};
    for (auto& texture : textures)
    {
        texture = MakeTexture();
    }
    auto first = Manager->PinTextureVersion(textures[1]);
    auto last  = Manager->PinTextureVersion(textures[7]);
    ASSERT_TRUE(first);
    ASSERT_TRUE(last);
    RenderResourceManager::TextureVersionSnapshot snapshot = {};
    ASSERT_TRUE(Manager->GetFrameTextureVersion(first, &snapshot));
    first.Reset();
    RRMDecodeTestHelper::StampResources(*Manager, 0, 3);
    Manager->FinishResourceFrame();
    ASSERT_TRUE(Manager->GetFrameTextureVersion(last, &snapshot));
    last.Reset();
    std::array<uint64_t, RenderResourceManager::ResourceTimelineCount> completed = {};
    completed[0]                                                                 = 3;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_EQ(RRMDecodeTestHelper::FrameTextureCount(*Manager), 1u);
    EXPECT_FALSE(Manager->IsTextureVersionPinned(textures[1]));
    EXPECT_TRUE(Manager->IsTextureVersionPinned(textures[7]));
    Manager->FinishResourceFrame();
    EXPECT_EQ(RRMDecodeTestHelper::FrameTextureCount(*Manager), 0u);
    EXPECT_FALSE(Manager->IsTextureVersionPinned(textures[7]));
}

TEST_F(ResourceVersionTest, SharedPinsMoveExactlyOnceAndBlockImageMutation)
{
    const auto handle = MakeTexture();
    auto       first  = Manager->PinTextureVersion(handle);
    auto       second = Manager->PinTextureVersion(handle);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first.Get()->Version, second.Get()->Version);
    EXPECT_EQ(first.Get()->Image.Index, handle.Index);
    EXPECT_EQ(first.Get()->Width, 16u);
    EXPECT_EQ(first.Get()->Height, 8u);
    EXPECT_EQ(first.Get()->Format, Specifications::ImageFormat::R8G8B8A8_SRGB);
    EXPECT_FALSE(Manager->CanWriteTextureVersion(handle));
    EXPECT_FALSE(Device->ReconstructTexture(handle, {}));
    EXPECT_FALSE(Device->ReconstructAliasingTexture(handle, {}, handle));
    uint8_t pixels[4] = {};
    EXPECT_FALSE(Manager->UploadTextureBuffer(0, 0, handle, pixels).Valid());
    EXPECT_FALSE(Manager->SubmitTextureFile("unused.png", handle).Valid());
    first = std::move(second);
    EXPECT_FALSE(second);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 1u);
    auto moved = std::move(first);
    EXPECT_FALSE(first);
    moved.Reset();
    moved.Reset();
    EXPECT_TRUE(Manager->CanWriteTextureVersion(handle));
}

TEST_F(ResourceVersionTest, DestructionWaitsForLastPinAndSealsAdmissionUntilGenerationReuse)
{
    auto handle = MakeTexture();
    auto first  = Manager->PinTextureVersion(handle);
    auto second = Manager->PinTextureVersion(handle);
    ASSERT_TRUE(first);
    const auto version = first.Get()->Version;
    Device->DestroyTexture(handle);
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    first.Reset();
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    second.Reset();
    EXPECT_EQ(RRMDecodeTestHelper::ReleaseCount(*Manager), 0u); // Retained in fixed retirement bitmap.
    RRMDecodeTestHelper::FlushReleases(*Manager);
    Hardwares::TextureDisposeEntry entry = {};
    ASSERT_TRUE(Device->TextureHandleToDispose.pop(entry));
    EXPECT_EQ(entry.Handle.Generation, handle.Generation);
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    EXPECT_FALSE(Manager->CanWriteTextureVersion(handle));
    const auto stale        = handle;
    auto       image_handle = Device->GlobalTextures.Access(handle)->BufferHandle;
    Device->ImageBufferManager.Remove(image_handle);
    Device->GlobalTextures.Remove(handle);
    const auto replacement = MakeTexture();
    EXPECT_EQ(replacement.Index, stale.Index);
    EXPECT_NE(replacement.Generation, stale.Generation);
    auto current = Manager->PinTextureVersion(replacement);
    ASSERT_TRUE(current);
    EXPECT_NE(current.Get()->Version, version);
    EXPECT_FALSE(Manager->PinTextureVersion(stale));
    EXPECT_FALSE(Manager->RetainTextureForRetirement(stale));
    auto shared = Manager->PinTextureVersion(replacement);
    EXPECT_TRUE(shared);
}

TEST_F(ResourceVersionTest, PendingReleaseAndIncompleteUploadsRefuseNewPins)
{
    const auto handle = MakeTexture();
    RRMDecodeTestHelper::QueueUnsubmittedUpload(*Manager);
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    RRMDecodeTestHelper::ClearUnsubmittedUploads(*Manager);
    RenderResourceManager::TextureDeferral deferral = {};
    deferral.TexHandle                              = handle;
    ASSERT_TRUE(Manager->EnqueueTextureDeferral(deferral));
    EXPECT_TRUE(Manager->HasPendingTextureUploads());
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    // Retain no fake deferral past teardown; discard uses no pixels/slab here.
    RRMDecodeTestHelper::DiscardDeferrals(*Manager);
    EXPECT_TRUE(Manager->PinTextureVersion(handle));
    RRMDecodeTestHelper::Track(*Manager, handle);
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    Manager->ForgetTextureDecode(handle);
    RRMDecodeTestHelper::ReleaseLater(*Manager, handle);
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    RRMDecodeTestHelper::FlushReleases(*Manager);
}

class ResourceVersionDecodeTest : public ResourceVersionTest
{
protected:
    MemoryManager DecodeMemory = {};

    void          SetUp() override
    {
        ResourceVersionTest::SetUp();
        DecodeMemory.Initialize(ZMega(136), {});
        RRMDecodeTestHelper::InitDecode(*Manager, DecodeMemory.MainArena, *Device);
        RRMDecodeTestHelper::InitDeferralRetry(*Manager, Memory.MainArena);
    }

    void TearDown() override
    {
        RRMDecodeTestHelper::DiscardDeferrals(*Manager);
        EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
        RRMDecodeTestHelper::ShutdownDecode(*Manager);
        ResourceVersionTest::TearDown();
    }
};

TEST_F(ResourceVersionDecodeTest, PinnedDecodeDeferralRetriesButDestructionReclaimsItsPixelsAndLease)
{
    const auto texture                                 = MakeTexture();
    Device->GlobalTextures.Access(texture)->BufferSize = 4;
    auto pin                                           = Manager->PinTextureVersion(texture);
    ASSERT_TRUE(pin);
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*Manager, texture));
    auto deferral = RRMDecodeTestHelper::MakeDeferral(*Manager, texture);
    ASSERT_NE(deferral.Pixels, nullptr);
    ASSERT_TRUE(Manager->EnqueueTextureDeferral(deferral));
    Manager->CompleteDeferrals(0);
    EXPECT_EQ(RRMDecodeTestHelper::DeferralRetryCount(*Manager), 1u);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 1u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Pending);
    std::thread release_owner([&] { Device->DestroyTexture(texture); });
    release_owner.join();
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    Manager->CompleteDeferrals(0);
    EXPECT_EQ(RRMDecodeTestHelper::DeferralRetryCount(*Manager), 0u);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    EXPECT_FALSE(Manager->CanWriteTextureVersion(texture));
    EXPECT_EQ(Manager->FindStreamingUploadTicket(texture), nullptr);
    pin.Reset();
    RRMDecodeTestHelper::FlushReleases(*Manager);
    Hardwares::TextureDisposeEntry disposal = {};
    EXPECT_TRUE(Device->TextureHandleToDispose.pop(disposal));
    EXPECT_EQ(disposal.Handle.Generation, texture.Generation);
    RRMDecodeTestHelper::FlushReleases(*Manager);
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
}

TEST_F(ResourceVersionDecodeTest, LateDecodeCannotModifyTheReusedTextureSlotOrKeepItsSlab)
{
    const auto texture = MakeTexture();
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*Manager, texture));
    auto deferral = RRMDecodeTestHelper::MakeDeferral(*Manager, texture);
    ASSERT_NE(deferral.Pixels, nullptr);
    auto image           = Device->GlobalTextures.Access(texture)->BufferHandle;
    auto removed_texture = texture;
    Device->ImageBufferManager.Remove(image);
    Device->GlobalTextures.Remove(removed_texture);
    const auto replacement = MakeTexture();
    ASSERT_EQ(replacement.Index, texture.Index);
    ASSERT_NE(replacement.Generation, texture.Generation);
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*Manager, replacement));
    EXPECT_TRUE(RRMDecodeTestHelper::ProcessDeferral(*Manager, deferral));
    EXPECT_EQ(deferral.Pixels, nullptr);
    EXPECT_EQ(deferral.DecodeSlabIndex, UINT8_MAX);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(replacement), RenderResourceManager::TextureDecodeState::Pending);
    EXPECT_TRUE(Manager->CanWriteTextureVersion(replacement));
    EXPECT_TRUE(Device->GlobalTextures.IsLive(replacement));
    // A consumed record is inert, including cleanup invoked twice by its owner.
    EXPECT_TRUE(RRMDecodeTestHelper::ProcessDeferral(*Manager, deferral));
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
}

TEST_F(ResourceVersionDecodeTest, PendingReleaseConsumesDecodedDeferralWithoutSubmittingNativeWork)
{
    const auto texture                                 = MakeTexture();
    Device->GlobalTextures.Access(texture)->BufferSize = 4;
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*Manager, texture));
    auto deferral = RRMDecodeTestHelper::MakeDeferral(*Manager, texture);
    ASSERT_NE(deferral.Pixels, nullptr);
    Manager->ReleaseTexture(texture);
    EXPECT_FALSE(Manager->CanWriteTextureVersion(texture));
    EXPECT_FALSE(Manager->BeginTextureVersionWrite(texture));
    EXPECT_TRUE(RRMDecodeTestHelper::ProcessDeferral(*Manager, deferral));
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    RRMDecodeTestHelper::FlushReleases(*Manager);
    Hardwares::TextureDisposeEntry disposal = {};
    EXPECT_TRUE(Device->TextureHandleToDispose.pop(disposal));
    EXPECT_EQ(disposal.Handle.Generation, texture.Generation);
}

TEST_F(ResourceVersionDecodeTest, ExplicitDiscardTerminatesTrackedDecodeAndReclaimsQueuedPixels)
{
    const auto texture = MakeTexture();
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*Manager, texture));
    const auto deferral = RRMDecodeTestHelper::MakeDeferral(*Manager, texture);
    ASSERT_NE(deferral.Pixels, nullptr);
    ASSERT_TRUE(Manager->EnqueueTextureDeferral(deferral));
    RRMDecodeTestHelper::DiscardDeferrals(*Manager);
    RRMDecodeTestHelper::DiscardDeferrals(*Manager);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    RRMDecodeTestHelper::Submitted(*Manager, texture);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
}

TEST_F(ResourceVersionDecodeTest, ReleasedQueuedDecodeDoesNotEnterTheWorkerPool)
{
    SaturatedBackgroundPool pool;
    // Hold the only worker but leave its queue empty. A submitted decode would
    // retain Pending == 1, so this proves cancellation before dispatch rather
    // than merely observing a missing/saturated pool's rejection callback.
    ASSERT_TRUE(
        Helpers::ThreadPoolHelper::SubmitToWorker(
            0, &pool, +[](void* context) {
                auto&            gate = *static_cast<SaturatedBackgroundPool*>(context);
                std::unique_lock lock(gate.Mutex);
                gate.Started = true;
                gate.CV.notify_all();
                gate.CV.wait(lock, [&] { return gate.Released; });
            }));
    {
        std::unique_lock lock(pool.Mutex);
        ASSERT_TRUE(pool.CV.wait_for(lock, std::chrono::seconds(2), [&] { return pool.Started; }));
    }
    const auto texture = MakeTexture();
    ASSERT_TRUE(RRMDecodeTestHelper::QueueDecode(*Manager, texture));
    Manager->ReleaseTexture(texture);
    RRMDecodeTestHelper::DispatchDecodes(*Manager);
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*Manager), 0u);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    EXPECT_TRUE(Device->DeferredTextureDescriptorUpdates.empty());
    EXPECT_FALSE(Manager->HasPendingTextureUploads());
    RRMDecodeTestHelper::FlushReleases(*Manager);
    Hardwares::TextureDisposeEntry disposal = {};
    EXPECT_TRUE(Device->TextureHandleToDispose.pop(disposal));
}

TEST_F(ResourceVersionDecodeTest, WorkerStartingAfterDestructionPublishesFailureWithoutPixelsOrDescriptors)
{
    const auto texture = MakeTexture();
    Device->DestroyTexture(texture);
    // Direct invocation represents an already-dispatched worker whose start was
    // delayed until after the release owner sealed this generation.
    EXPECT_TRUE(RRMDecodeTestHelper::SubmitDecode(*Manager, "must-not-be-read.png", 4, false, texture));
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*Manager), 0u);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    EXPECT_TRUE(Device->DeferredTextureDescriptorUpdates.empty());
    EXPECT_FALSE(Manager->HasPendingTextureUploads());
    Hardwares::TextureDisposeEntry disposal = {};
    EXPECT_TRUE(Device->TextureHandleToDispose.pop(disposal));
    EXPECT_EQ(disposal.Handle.Generation, texture.Generation);
}

TEST_F(ResourceVersionDecodeTest, FailedOrOversizedDeferralsAreTerminalInsteadOfPermanentRetries)
{
    const auto texture                                 = MakeTexture();
    Device->GlobalTextures.Access(texture)->BufferSize = 4;
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*Manager, texture));
    auto deferral     = RRMDecodeTestHelper::MakeDeferral(*Manager, texture);
    deferral.ByteSize = 5; // Rejected before accessing the four-byte allocation.
    EXPECT_TRUE(RRMDecodeTestHelper::ProcessDeferral(*Manager, deferral));
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_EQ(Manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    deferral = RRMDecodeTestHelper::MakeDeferral(*Manager, texture);
    EXPECT_TRUE(RRMDecodeTestHelper::ProcessDeferral(*Manager, deferral));
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*Manager), 0u);
    EXPECT_FALSE(Manager->HasPendingTextureUploads());
}

TEST_F(ResourceVersionTest, MaterialCopiesShaderValuesAndRetainsAllFiveMapsWithoutFallback)
{
    const auto           first  = MakeTexture();
    const auto           second = MakeTexture();
    Meshes::MeshMaterial source = {};
    source.AlbedoMap            = static_cast<uint32_t>(first.Index);
    source.EmissiveMap          = static_cast<uint32_t>(second.Index);
    source.NormalMap            = source.AlbedoMap;
    source.OpacityMap           = source.EmissiveMap;
    source.SpecularMap          = source.AlbedoMap;
    source.AlbedoColor          = {0.1f, 0.2f, 0.3f, 0.4f};
    source.Factors.z            = 0.25f;
    source.AlphaMode            = Materials::AlphaMode::Blend;
    auto pin                    = RRMDecodeTestHelper::RetainMaterial(*Manager, source);
    ASSERT_TRUE(pin);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 5u);
    EXPECT_EQ(pin.GetTexture(0)->Version, pin.GetTexture(2)->Version);
    EXPECT_EQ(pin.GetTexture(1)->Version, pin.GetTexture(3)->Version);
    EXPECT_EQ(pin.GetTexture(0)->Version, pin.GetTexture(4)->Version);
    EXPECT_EQ(pin.GetTexture(5), nullptr);
    source = {};
    EXPECT_FLOAT_EQ(pin.Get()->Material.AlbedoColor.w, 0.4f);
    EXPECT_FLOAT_EQ(pin.Get()->Material.Factors.z, 0.25f);
    EXPECT_EQ(pin.Get()->Material.AlphaMode, Materials::AlphaMode::Blend);
    auto moved = std::move(pin);
    EXPECT_FALSE(pin);
    EXPECT_TRUE(moved);
    auto assigned = RRMDecodeTestHelper::RetainMaterial(*Manager, {});
    assigned      = std::move(moved);
    EXPECT_FALSE(moved);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 5u);
    assigned.Reset();
    assigned.Reset();
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
}

TEST_F(ResourceVersionTest, MissingMapRollsBackEarlierPinsAndOptionalMapsNeedNoTexture)
{
    const auto           handle   = MakeTexture();
    Meshes::MeshMaterial material = {};
    material.AlbedoMap            = static_cast<uint32_t>(handle.Index);
    material.EmissiveMap          = 7; // In range, but no live texture in this slot.
    EXPECT_FALSE(RRMDecodeTestHelper::RetainMaterial(*Manager, material));
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
    material.EmissiveMap = 8; // Out of the device's descriptor range.
    EXPECT_FALSE(RRMDecodeTestHelper::RetainMaterial(*Manager, material));
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
    auto optional = RRMDecodeTestHelper::RetainMaterial(*Manager, {});
    ASSERT_TRUE(optional);
    EXPECT_EQ(optional.GetTexture(0), nullptr);
    EXPECT_FALSE(Manager->PinMaterialVersion({}));
    EXPECT_FALSE(Manager->PinMaterialVersion(MakeUUID()));
}

TEST_F(ResourceVersionTest, ExhaustedIdentitiesAndShutdownAdmissionCannotInvalidateExistingPins)
{
    const auto handle = MakeTexture();
    auto       first  = Manager->PinTextureVersion(handle);
    ASSERT_TRUE(first);
    const auto version = first.Get()->Version;
    RRMDecodeTestHelper::ExhaustVersionIds(*Manager);
    auto shared = Manager->PinTextureVersion(handle);
    ASSERT_TRUE(shared);
    EXPECT_FALSE(Manager->PinTextureVersion(MakeTexture()));
    EXPECT_FALSE(RRMDecodeTestHelper::RetainMaterial(*Manager, {}));
    EXPECT_FALSE(Manager->PinTextureVersion({}));
    EXPECT_FALSE(Manager->PinTextureVersion({RenderResourceManager::TextureVersionCapacity, 0}));
    RRMDecodeTestHelper::SealVersions(*Manager);
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    EXPECT_EQ(first.Get()->Version, version);
}

TEST_F(ResourceVersionTest, ProducerRetirementCannotLoseOrReplaceRetainedVersion)
{
    for (uint32_t cycle = 0; cycle < 500; ++cycle)
    {
        auto handle = MakeTexture();
        auto pin    = Manager->PinTextureVersion(handle);
        ASSERT_TRUE(pin);
        const auto  version = pin.Get()->Version;
        std::thread producer([&] {
            EXPECT_TRUE(Manager->IsTextureVersionPinned(handle));
            EXPECT_TRUE(Manager->RetainTextureForRetirement(handle));
        });
        producer.join();
        EXPECT_FALSE(Manager->PinTextureVersion(handle));
        EXPECT_EQ(pin.Get()->Version, version);
        pin.Reset();
        RRMDecodeTestHelper::FlushReleases(*Manager);
        Hardwares::TextureDisposeEntry entry = {};
        EXPECT_TRUE(Device->TextureHandleToDispose.pop(entry));
        auto image_handle = Device->GlobalTextures.Access(handle)->BufferHandle;
        Device->ImageBufferManager.Remove(image_handle);
        Device->GlobalTextures.Remove(handle);
        EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
    }
}

TEST_F(ResourceVersionTest, MissingImageAndAliasedBackingRefuseWithoutPinning)
{
    const auto handle                 = MakeTexture();
    auto*      texture                = Device->GlobalTextures.Access(handle);
    auto*      image                  = Device->ImageBufferManager.Access(texture->BufferHandle);
    image->GetBuffer().OwnsAllocation = false;
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    image->GetBuffer().OwnsAllocation  = true;
    texture->Specification.IsAliasable = true;
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    texture->Specification.IsAliasable = false;
    auto image_handle                  = texture->BufferHandle;
    Device->ImageBufferManager.Remove(image_handle);
    EXPECT_FALSE(Manager->PinTextureVersion(handle));
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
}

TEST_F(ResourceVersionTest, FinalPinRetirementSurvivesReleaseQueueSaturation)
{
    const auto handle = MakeTexture();
    auto       pin    = Manager->PinTextureVersion(handle);
    ASSERT_TRUE(pin);
    Device->DestroyTexture(handle);
    pin.Reset();
    EXPECT_TRUE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
    RRMDecodeTestHelper::AdmitTextureRetirements(*Manager, true);
    EXPECT_TRUE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    RRMDecodeTestHelper::AdmitTextureRetirements(*Manager, false, true);
    EXPECT_FALSE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
    EXPECT_EQ(RRMDecodeTestHelper::ReleaseCount(*Manager), 1u);
    RRMDecodeTestHelper::FlushReleases(*Manager);
    Hardwares::TextureDisposeEntry entry = {};
    ASSERT_TRUE(Device->TextureHandleToDispose.pop(entry));
    EXPECT_EQ(entry.Handle.Index, handle.Index);
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
}

TEST_F(ResourceVersionTest, UnpinnedFireAndForgetReleaseSurvivesSaturationWithoutLosingItsOwner)
{
    const auto handle = MakeTexture();
    RRMDecodeTestHelper::AdmitTextureRetirements(*Manager, true);
    Manager->ReleaseTexture(handle);
    EXPECT_TRUE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    RRMDecodeTestHelper::AdmitTextureRetirements(*Manager, false, true);
    EXPECT_FALSE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
    EXPECT_EQ(RRMDecodeTestHelper::ReleaseCount(*Manager), 1u);
    RRMDecodeTestHelper::FlushReleases(*Manager);
    Hardwares::TextureDisposeEntry entry = {};
    ASSERT_TRUE(Device->TextureHandleToDispose.pop(entry));
    EXPECT_EQ(entry.Handle, handle);
    EXPECT_TRUE(Device->TextureHandleToDispose.empty());
}

TEST_F(ResourceVersionTest, NativeQueueRefusalPreservesUnpinnedAndFinalPinRetirements)
{
    for (const bool retain_first : {false, true})
    {
        SCOPED_TRACE(retain_first);
        const auto handle = MakeTexture();
        auto       pin    = retain_first ? Manager->PinTextureVersion(handle) : RenderResourceManager::TextureVersionPin{};
        EXPECT_EQ(bool(pin), retain_first);
        const Hardwares::TextureDisposeEntry filler          = {};
        constexpr uint32_t                   usable_capacity = Hardwares::TextureDisposeQueue::CAPACITY - 1;
        for (uint32_t index = 0; index < usable_capacity; ++index)
        {
            ASSERT_TRUE(Device->TextureHandleToDispose.push(filler));
        }
        EXPECT_FALSE(Device->TextureHandleToDispose.push(filler));

        Device->DestroyTexture(handle);
        pin.Reset();
        EXPECT_TRUE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
        for (uint32_t pump = 0; pump < 2; ++pump)
        {
            RRMDecodeTestHelper::FlushReleases(*Manager);
            EXPECT_TRUE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
            EXPECT_EQ(RRMDecodeTestHelper::ReleaseCount(*Manager), 0u);
            EXPECT_FALSE(Manager->PinTextureVersion(handle));
            EXPECT_FALSE(Manager->CanWriteTextureVersion(handle));
        }
        Hardwares::TextureDisposeEntry entry = {};
        for (uint32_t index = 0; index < usable_capacity; ++index)
        {
            ASSERT_TRUE(Device->TextureHandleToDispose.pop(entry));
            EXPECT_FALSE(entry.Handle.Valid());
        }
        EXPECT_TRUE(Device->TextureHandleToDispose.empty());
        RRMDecodeTestHelper::FlushReleases(*Manager);
        EXPECT_FALSE(RRMDecodeTestHelper::RetirementWaiting(*Manager, handle.Index));
        ASSERT_TRUE(Device->TextureHandleToDispose.pop(entry));
        EXPECT_EQ(entry.Handle.Index, handle.Index);
        EXPECT_EQ(entry.Handle.Generation, handle.Generation);
        EXPECT_TRUE(Device->TextureHandleToDispose.empty());
        RRMDecodeTestHelper::FlushReleases(*Manager);
        EXPECT_TRUE(Device->TextureHandleToDispose.empty());
    }
}

TEST_F(ResourceVersionTest, RetirementsRespectThePerPumpSlice)
{
    Device->MaxGlobalTexture = 32;
    Device->GlobalTextures.Initialize(&Memory.MainArena, 32);
    Device->ImageBufferManager.Initialize(&Memory.MainArena, 32);
    std::vector<RenderResourceManager::TextureVersionPin> pins = {};
    for (uint32_t i = 0; i < 32; ++i)
    {
        const auto handle = MakeTexture();
        auto       pin    = Manager->PinTextureVersion(handle);
        ASSERT_TRUE(pin);
        Device->DestroyTexture(handle);
        pins.push_back(std::move(pin));
    }
    pins.clear();
    for (uint32_t pump = 0; pump < 2; ++pump)
    {
        RRMDecodeTestHelper::FlushReleases(*Manager);
        uint32_t                       retired = 0;
        Hardwares::TextureDisposeEntry entry   = {};
        while (Device->TextureHandleToDispose.pop(entry))
        {
            ++retired;
        }
        EXPECT_EQ(retired, RenderResourceManager::TextureRetirementSlice);
    }
}

TEST(MaterialVersionCacheTest, UuidLookupCopiesMaterialAndSurvivesCpuCacheReplacement)
{
    ASSERT_EQ(AssetManager::Instance(), nullptr);
    MemoryManager memory = {};
    memory.Initialize(ZMega(512), {});
    AssetManager::Initialize(&memory.MainArena, nullptr, "/project");
    struct CacheGuard
    {
        ~CacheGuard()
        {
            AssetManager::Shutdown();
        }
    } guard                           = {};
    auto                     manager  = std::make_unique<RenderResourceManager>();
    Importers::AssetMaterial material = {};
    material.MaterialUUID             = MakeUUID();
    material.AlbedoColor[3]           = 0.25f;
    material.AlphaMode                = Materials::AlphaMode::Blend;
    const auto uuid                   = material.MaterialUUID;
    AssetManager::IngestMaterial(std::move(material));
    auto original = manager->PinMaterialVersion(uuid);
    ASSERT_TRUE(original);
    uint32_t                                       frame_index    = 0;
    RenderResourceManager::MaterialVersionSnapshot frame_material = {};
    ASSERT_TRUE(manager->GetFrameMaterialVersion(uuid, &frame_index, &frame_material));
    EXPECT_FALSE(manager->GetFrameMaterialVersion(MakeUUID(), &frame_index, &frame_material));
    {
        auto*           assets = AssetManager::Instance();
        std::lock_guard lock(assets->IngestMutex);
        const auto*     slot = assets->UUIDToMaterialSlot.find(uuid);
        ASSERT_NE(slot, nullptr);
        assets->GPUMeshMaterials[*slot].AlbedoColor.w = 1.f;
        assets->GPUMeshMaterials[*slot].AlphaMode     = Materials::AlphaMode::Opaque;
    }
    auto replacement = manager->PinMaterialVersion(uuid);
    ASSERT_TRUE(replacement);
    EXPECT_NE(replacement.Get()->Version, original.Get()->Version);
    EXPECT_FLOAT_EQ(original.Get()->Material.AlbedoColor.w, 0.25f);
    EXPECT_EQ(original.Get()->Material.AlphaMode, Materials::AlphaMode::Blend);
    EXPECT_FLOAT_EQ(replacement.Get()->Material.AlbedoColor.w, 1.f);
    EXPECT_EQ(replacement.Get()->Material.AlphaMode, Materials::AlphaMode::Opaque);
    const auto original_frame = frame_material;
    ASSERT_TRUE(manager->GetFrameMaterialVersion(uuid, &frame_index, &frame_material));
    EXPECT_NE(original_frame.Version, frame_material.Version);
    EXPECT_FLOAT_EQ(original_frame.Material.AlbedoColor.w, .25f);
    EXPECT_FLOAT_EQ(frame_material.Material.AlbedoColor.w, 1.f);
    EXPECT_TRUE(manager->GetFrameMaterialVersion({}, &frame_index, &frame_material, true));
    manager->FinishResourceFrame();
    EXPECT_FALSE(manager->PinMaterialVersion(MakeUUID()));
}

class FrameMaterialTest : public ResourceVersionTest
{
protected:
    void TearDown() override
    {
        RRMDecodeTestHelper::ReleaseFrameResources(*Manager);
        ResourceVersionTest::TearDown();
    }
};

TEST_F(FrameMaterialTest, RetainedTransferRollsBackAllClonedMapsOnReferenceOverflow)
{
    const auto           texture  = MakeTexture();
    Meshes::MeshMaterial material = {};
    material.AlbedoMap            = static_cast<uint32_t>(texture.Index);
    material.OpacityMap           = material.AlbedoMap;
    auto retained                 = RRMDecodeTestHelper::RetainMaterial(*Manager, material);
    ASSERT_TRUE(retained);
    const auto                                     previous = RRMDecodeTestHelper::SetTextureReferences(*Manager, texture, UINT32_MAX - 1);
    RenderResourceManager::MaterialVersionSnapshot snapshot = {};
    snapshot.Version                                        = 123;
    uint32_t index                                          = 456;
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(retained, &index, &snapshot));
    EXPECT_EQ(RRMDecodeTestHelper::SetTextureReferences(*Manager, texture, previous), UINT32_MAX - 1);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 2u);
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 0u);
    EXPECT_EQ(snapshot.Version, 123u);
    EXPECT_EQ(index, 456u);
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(retained, &index, &snapshot));
    EXPECT_EQ(snapshot.Version, retained.Get()->Version);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 4u);
    retained.Reset();
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 2u);
    Manager->FinishResourceFrame();
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
}

TEST_F(FrameMaterialTest, ForeignOrEmptyLeasesCannotReuseAnExistingFrameEntry)
{
    const auto           texture  = MakeTexture();
    Meshes::MeshMaterial material = {};
    material.AlbedoMap            = static_cast<uint32_t>(texture.Index);
    auto retained                 = RRMDecodeTestHelper::RetainMaterial(*Manager, material);
    ASSERT_TRUE(retained);
    auto foreign = std::make_unique<RenderResourceManager>();
    RRMDecodeTestHelper::SetDevice(*foreign, Device.get());
    uint32_t                                       index    = 0;
    RenderResourceManager::MaterialVersionSnapshot snapshot = {};
    ASSERT_TRUE(foreign->GetFrameMaterialVersion(material, &index, &snapshot));
    EXPECT_FALSE(foreign->GetFrameMaterialVersion(retained, &index, &snapshot));
    const RenderResourceManager::MaterialVersionPin empty = {};
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(empty, &index, &snapshot));
    foreign->FinishResourceFrame();
    retained.Reset();
}

TEST_F(FrameMaterialTest, RepeatedDrawsShareVersionAndUnsubmittedRecordingReleasesIt)
{
    const auto           texture                         = MakeTexture();
    Meshes::MeshMaterial material                        = {};
    material.AlbedoMap                                   = static_cast<uint32_t>(texture.Index);
    material.OpacityMap                                  = material.AlbedoMap;
    uint32_t                                       first = UINT32_MAX, second = UINT32_MAX;
    RenderResourceManager::MaterialVersionSnapshot original = {}, shared = {};
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(material, nullptr, &original));
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &first, &original));
    for (uint32_t draw = 0; draw < 1000; ++draw)
    {
        ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &second, &shared));
        EXPECT_EQ(second, first);
        EXPECT_EQ(shared.Version, original.Version);
    }
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 1u);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 2u);
    Manager->FinishResourceFrame();
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 0u);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
}

TEST_F(FrameMaterialTest, EveryAcceptedQueueAndPresentationGateProtectsTheRetainedImages)
{
    const auto           texture                            = MakeTexture();
    Meshes::MeshMaterial material                           = {};
    material.AlbedoMap                                      = static_cast<uint32_t>(texture.Index);
    uint32_t                                       index    = 0;
    RenderResourceManager::MaterialVersionSnapshot snapshot = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    std::array<uint64_t, RenderResourceManager::ResourceTimelineCount> completed = {};
    for (uint32_t queue = 0; queue < completed.size(); ++queue)
    {
        RRMDecodeTestHelper::StampResources(*Manager, queue, queue + 3);
    }
    Manager->FinishResourceFrame();
    for (uint32_t queue = 0; queue < completed.size(); ++queue)
    {
        EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
        completed[queue] = queue + 3;
        RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    }
    EXPECT_FALSE(Manager->IsTextureVersionPinned(texture));
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 0u);
}

TEST_F(FrameMaterialTest, AbortedPresentationPreservesEarlierGraphWorkAndLaterRecordingExtendsIt)
{
    const auto           texture                         = MakeTexture();
    Meshes::MeshMaterial material                        = {};
    material.AlbedoMap                                   = static_cast<uint32_t>(texture.Index);
    uint32_t                                       index = 0, reused = 0;
    RenderResourceManager::MaterialVersionSnapshot snapshot = {}, next = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    RRMDecodeTestHelper::StampResources(*Manager, 0, 7);
    Manager->FinishResourceFrame(); // no presentation accepted
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &reused, &next));
    EXPECT_EQ(reused, index);
    EXPECT_EQ(snapshot.Version, next.Version);
    std::array<uint64_t, RenderResourceManager::ResourceTimelineCount> completed = {};
    completed[0]                                                                 = 7;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_TRUE(Manager->IsTextureVersionPinned(texture)); // recording still owns it
    RRMDecodeTestHelper::StampResources(*Manager, 0, 9);
    Manager->FinishResourceFrame();
    EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
    completed[0] = 9;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_FALSE(Manager->IsTextureVersionPinned(texture));
}

TEST_F(FrameMaterialTest, CacheValueChangesAdmitNewVersionWithoutChangingOlderCopy)
{
    const auto           texture                             = MakeTexture();
    Meshes::MeshMaterial material                            = {};
    material.AlbedoMap                                       = static_cast<uint32_t>(texture.Index);
    material.AlbedoColor.w                                   = .25f;
    uint32_t                                       old_index = 0, new_index = 0;
    RenderResourceManager::MaterialVersionSnapshot old = {}, current = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &old_index, &old));
    RRMDecodeTestHelper::StampResources(*Manager, 0, 3);
    Manager->FinishResourceFrame();
    material.AlbedoColor.w = .75f;
    material.AlphaMode     = Materials::AlphaMode::Blend;
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &new_index, &current));
    EXPECT_NE(old_index, new_index);
    EXPECT_NE(old.Version, current.Version);
    EXPECT_FLOAT_EQ(old.Material.AlbedoColor.w, .25f);
    EXPECT_EQ(current.Material.AlphaMode, Materials::AlphaMode::Blend);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 2u);
    RRMDecodeTestHelper::StampResources(*Manager, 0, 5);
    Manager->FinishResourceFrame();
    std::array<uint64_t, RenderResourceManager::ResourceTimelineCount> completed = {};
    completed[0]                                                                 = 3;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 1u);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 1u);
    completed[0] = 5;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
}

TEST_F(FrameMaterialTest, ExistingRetainedVersionRemainsDrawableWhileUnrelatedUploadsRun)
{
    const auto           texture                         = MakeTexture();
    Meshes::MeshMaterial material                        = {};
    material.AlbedoMap                                   = static_cast<uint32_t>(texture.Index);
    uint32_t                                       index = 0, reused = 0;
    RenderResourceManager::MaterialVersionSnapshot snapshot = {}, next = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    RRMDecodeTestHelper::QueueUnsubmittedUpload(*Manager);
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &reused, &next));
    EXPECT_EQ(reused, index);
    EXPECT_EQ(snapshot.Version, next.Version);
    material.AlbedoColor.w = .5f;
    EXPECT_TRUE(Manager->GetFrameMaterialVersion(material, &reused, &next));
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 2u);
    const auto uninitialized = MakeTexture();
    material.AlbedoMap       = static_cast<uint32_t>(uninitialized.Index);
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(material, &reused, &next));
    RRMDecodeTestHelper::ClearUnsubmittedUploads(*Manager);
}

TEST_F(FrameMaterialTest, ManagedWriteInvalidatesReadmissionProofAfterEarlierFrameRetires)
{
    const auto           texture                            = MakeTexture();
    Meshes::MeshMaterial material                           = {};
    material.AlbedoMap                                      = static_cast<uint32_t>(texture.Index);
    uint32_t                                       index    = 0;
    RenderResourceManager::MaterialVersionSnapshot snapshot = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    EXPECT_FALSE(Manager->BeginTextureVersionWrite(texture));
    Manager->FinishResourceFrame();
    EXPECT_FALSE(Manager->IsTextureVersionPinned(texture));
    RRMDecodeTestHelper::QueueUnsubmittedUpload(*Manager);
    EXPECT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    Manager->FinishResourceFrame();
    EXPECT_TRUE(Manager->BeginTextureVersionWrite(texture));
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    RRMDecodeTestHelper::ClearUnsubmittedUploads(*Manager);
    EXPECT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
}

TEST_F(FrameMaterialTest, FixedCapacityRefusesNewVersionsAndReusesRetiredBucketSlots)
{
    Meshes::MeshMaterial                           material = {};
    uint32_t                                       index    = 0;
    RenderResourceManager::MaterialVersionSnapshot snapshot = {};
    for (uint32_t version = 0; version < RenderResourceManager::FrameMaterialCapacity; ++version)
    {
        material.Factors.x = static_cast<float>(version);
        ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    }
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), RenderResourceManager::FrameMaterialCapacity);
    material.Factors.x = -1.f;
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    material.Factors.x = 1.f;
    EXPECT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    Manager->FinishResourceFrame();
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 0u);
    material.Factors.x = -1.f;
    EXPECT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
}

TEST_F(FrameMaterialTest, MissingMapRollsBackAndSealedAdmissionCannotInvalidateEarlierVersions)
{
    const auto           texture                            = MakeTexture();
    Meshes::MeshMaterial material                           = {};
    material.AlbedoMap                                      = static_cast<uint32_t>(texture.Index);
    material.OpacityMap                                     = 7;
    uint32_t                                       index    = 0;
    RenderResourceManager::MaterialVersionSnapshot snapshot = {};
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    EXPECT_EQ(RRMDecodeTestHelper::VersionPins(*Manager), 0u);
    material.OpacityMap = INVALID_MAP_HANDLE;
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    RRMDecodeTestHelper::SealVersions(*Manager);
    EXPECT_FALSE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    EXPECT_TRUE(Manager->IsTextureVersionPinned(texture));
    Manager->FinishResourceFrame();
    EXPECT_FALSE(Manager->IsTextureVersionPinned(texture));
}

TEST_F(FrameMaterialTest, EveryShaderFieldParticipatesInVersionDeduplication)
{
    Meshes::MeshMaterial                           material       = {};
    uint32_t                                       original_index = 0, index = 0;
    RenderResourceManager::MaterialVersionSnapshot original = {}, snapshot = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &original_index, &original));
    Core::Maths::Vec4f Meshes::MeshMaterial::* colors[] = {&Meshes::MeshMaterial::AmbientColor, &Meshes::MeshMaterial::EmissiveColor, &Meshes::MeshMaterial::AlbedoColor, &Meshes::MeshMaterial::SpecularColor, &Meshes::MeshMaterial::RoughnessColor, &Meshes::MeshMaterial::Factors};
    for (const auto field : colors)
    {
        for (uint32_t component = 0; component < 4; ++component)
        {
            auto changed                 = material;
            (changed.*field)[component] += .5f;
            ASSERT_TRUE(Manager->GetFrameMaterialVersion(changed, &index, &snapshot));
            EXPECT_NE(index, original_index);
            EXPECT_NE(snapshot.Version, original.Version);
        }
    }
    const auto texture                      = MakeTexture();
    uint32_t Meshes::MeshMaterial::* maps[] = {&Meshes::MeshMaterial::AlbedoMap, &Meshes::MeshMaterial::EmissiveMap, &Meshes::MeshMaterial::NormalMap, &Meshes::MeshMaterial::OpacityMap, &Meshes::MeshMaterial::SpecularMap};
    for (const auto field : maps)
    {
        auto changed   = material;
        changed.*field = static_cast<uint32_t>(texture.Index);
        ASSERT_TRUE(Manager->GetFrameMaterialVersion(changed, &index, &snapshot));
        EXPECT_NE(index, original_index);
    }
    material.AlphaMode = Materials::AlphaMode::Blend;
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material, &index, &snapshot));
    EXPECT_NE(index, original_index);
    EXPECT_EQ(RRMDecodeTestHelper::FrameMaterialCount(*Manager), 31u);
}

class TextureReplacementTest : public ResourceVersionTest
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(AssetManager::Instance(), nullptr);
        Memory.Initialize(ZMega(512), {});
        Device->Arena            = &Memory.MainArena;
        Device->SwapchainPtr     = &Swapchain;
        Device->MaxGlobalTexture = 32;
        Device->GlobalTextures.Initialize(&Memory.MainArena, 32);
        Device->ImageBufferManager.Initialize(&Memory.MainArena, 32);
        Device->RRM = Manager.get();
        RRMDecodeTestHelper::SetDevice(*Manager, Device.get());
        AssetManager::Initialize(&Memory.MainArena, nullptr, "/project");
    }
    void TearDown() override
    {
        RRMDecodeTestHelper::ReleaseFrameResources(*Manager);
        AssetManager::Shutdown();
        ResourceVersionTest::TearDown();
    }
    uuids::uuid Register(Textures::TextureHandle texture)
    {
        const auto uuid   = MakeUUID();
        auto*      assets = AssetManager::Instance();
        auto&      asset  = assets->Textures.push_use({});
        asset.TextureUUID = uuid;
        asset.Handle      = texture;
        assets->UUIDToTextureHandle.insert(uuid, texture);
        return uuid;
    }
};

TEST_F(TextureReplacementTest, PublicationUpdatesAllMapsButPreservesRetainedMaterialAndImage)
{
    const auto               old_texture = MakeTexture();
    const auto               candidate   = MakeTexture();
    const auto               uuid        = Register(old_texture);
    Importers::AssetMaterial material    = {};
    material.MaterialUUID                = MakeUUID();
    material.AlbedoTexUUID = material.EmissiveTexUUID = material.NormalTexUUID = material.OpacityTexUUID = material.SpecularTexUUID = uuid;
    const auto material_uuid                                                                                                        = material.MaterialUUID;
    AssetManager::IngestMaterial(std::move(material));
    auto retained = Manager->PinMaterialVersion(material_uuid);
    ASSERT_TRUE(retained);
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, old_texture, candidate));
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, candidate.Index);
    EXPECT_EQ(AssetManager::Instance()->Textures[0].Handle.Index, candidate.Index);
    auto current = Manager->PinMaterialVersion(material_uuid);
    ASSERT_TRUE(current);
    for (uint32_t index = 0; index < 5; ++index)
    {
        ASSERT_NE(current.GetTexture(index), nullptr);
        ASSERT_NE(retained.GetTexture(index), nullptr);
        EXPECT_EQ(current.GetTexture(index)->Handle.Index, candidate.Index);
        EXPECT_EQ(retained.GetTexture(index)->Handle.Index, old_texture.Index);
    }
    EXPECT_TRUE(Manager->GetTexture(old_texture));
    EXPECT_FALSE(Manager->CanWriteTextureVersion(old_texture));
    EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, old_texture));
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 0u);
}

TEST_F(TextureReplacementTest, AutomaticFrameVersionsRemainIndependentAcrossCopyOnWritePublication)
{
    const auto               previous  = MakeTexture();
    const auto               candidate = MakeTexture();
    const auto               uuid      = Register(previous);
    Importers::AssetMaterial authored  = {};
    authored.MaterialUUID              = MakeUUID();
    authored.AlbedoTexUUID             = uuid;
    const auto material_uuid           = authored.MaterialUUID;
    AssetManager::IngestMaterial(std::move(authored));
    uint32_t                                       old_index = 0, current_index = 0;
    RenderResourceManager::MaterialVersionSnapshot old = {}, current = {};
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material_uuid, &old_index, &old));
    RRMDecodeTestHelper::StampResources(*Manager, 0, 3);
    Manager->FinishResourceFrame();
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate));
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    ASSERT_TRUE(Manager->GetFrameMaterialVersion(material_uuid, &current_index, &current));
    EXPECT_NE(old_index, current_index);
    EXPECT_EQ(old.Material.AlbedoMap, previous.Index);
    EXPECT_EQ(current.Material.AlbedoMap, candidate.Index);
    EXPECT_TRUE(Manager->IsTextureVersionPinned(previous));
    EXPECT_TRUE(Manager->IsTextureVersionPinned(candidate));
    RRMDecodeTestHelper::StampResources(*Manager, 0, 5);
    Manager->FinishResourceFrame();
    std::array<uint64_t, RenderResourceManager::ResourceTimelineCount> completed = {};
    completed[0]                                                                 = 3;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_FALSE(Manager->IsTextureVersionPinned(previous));
    EXPECT_TRUE(Manager->IsTextureVersionPinned(candidate));
    completed[0] = 5;
    RRMDecodeTestHelper::CompleteResources(*Manager, completed);
    EXPECT_FALSE(Manager->IsTextureVersionPinned(candidate));
}

TEST_F(TextureReplacementTest, SubmittedOrPendingDecodeAloneCannotPublish)
{
    const auto previous  = MakeTexture();
    const auto candidate = MakeTexture();
    const auto uuid      = Register(previous);
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate, false));
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 1u);
    EXPECT_TRUE(Manager->HasPendingTextureUploads());
    RRMDecodeTestHelper::CancelReplacements(*Manager);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
}

TEST_F(TextureReplacementTest, FailedReloadPreservesMappingAndRetiresOnlyCandidate)
{
    const auto previous  = MakeTexture();
    const auto candidate = MakeTexture();
    const auto uuid      = Register(previous);
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate));
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, false);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
    EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
    EXPECT_FALSE(RRMDecodeTestHelper::ReleaseQueued(*Manager, previous));
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 0u);
}

TEST_F(TextureReplacementTest, LaterReloadAndPendingDeletionBothRejectObsoletePublication)
{
    for (bool deleting : {false, true})
    {
        const auto previous  = MakeTexture();
        const auto candidate = MakeTexture();
        const auto uuid      = Register(previous);
        ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate));
        RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
        if (deleting)
        {
            AssetManager::ReleaseTexture(uuid);
        }
        else
        {
            Manager->ScheduleTextureReload(uuid);
        }
        RRMDecodeTestHelper::PollReplacements(*Manager);
        EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
        EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
        EXPECT_FALSE(RRMDecodeTestHelper::ReleaseQueued(*Manager, previous));
    }
}

TEST_F(TextureReplacementTest, FallbackAndOtherUuidReferencesAreNotRetired)
{
    for (bool fallback : {false, true})
    {
        const auto previous  = MakeTexture();
        const auto candidate = MakeTexture();
        const auto uuid      = Register(previous);
        if (fallback)
        {
            AssetManager::Instance()->FallbackTextureHandle = previous;
        }
        else
        {
            Register(previous);
        }
        ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate));
        RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
        RRMDecodeTestHelper::PollReplacements(*Manager);
        EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, candidate.Index);
        EXPECT_FALSE(RRMDecodeTestHelper::ReleaseQueued(*Manager, previous));
    }
}

TEST_F(TextureReplacementTest, ChangedExpectedGenerationCannotPublish)
{
    const auto previous  = MakeTexture();
    const auto candidate = MakeTexture();
    const auto uuid      = Register(previous);
    auto       stale     = previous;
    ++stale.Generation;
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, stale, candidate));
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
    EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
}

TEST_F(TextureReplacementTest, CancellationKeepsWorkerOwnedCandidateUntilCompletion)
{
    const auto previous  = MakeTexture();
    const auto candidate = MakeTexture();
    const auto uuid      = Register(previous);
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate));
    RRMDecodeTestHelper::CancelReplacements(*Manager);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 1u);
    EXPECT_FALSE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 0u);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, previous.Index);
    EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
}

TEST_F(TextureReplacementTest, FullReleaseQueueRetainsPublishedReplacementRetirementWithoutRepublishing)
{
    const auto previous  = MakeTexture();
    const auto candidate = MakeTexture();
    const auto uuid      = Register(previous);
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate));
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
    RRMDecodeTestHelper::AdmitTextureRetirements(*Manager, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, candidate.Index);
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 1u);
    RRMDecodeTestHelper::AdmitTextureRetirements(*Manager, false, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, candidate.Index);
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 0u);
    EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, previous));
    EXPECT_FALSE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
}

TEST_F(TextureReplacementTest, ReplacementCapacityRefusesExtraWorkWithoutLosingAdmittedCandidates)
{
    const auto              previous       = MakeTexture();
    Textures::TextureHandle candidates[16] = {};
    for (auto& candidate : candidates)
    {
        candidate = MakeTexture();
        ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, Register(previous), previous, candidate));
    }
    EXPECT_FALSE(RRMDecodeTestHelper::StageReplacement(*Manager, Register(previous), previous, MakeTexture()));
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 16u);
    RRMDecodeTestHelper::CancelReplacements(*Manager);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 16u);
    for (const auto candidate : candidates)
    {
        RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, false);
    }
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(RRMDecodeTestHelper::ReplacementCount(*Manager), 0u);
    for (const auto candidate : candidates)
    {
        EXPECT_TRUE(RRMDecodeTestHelper::ReleaseQueued(*Manager, candidate));
    }
}

TEST_F(TextureReplacementTest, DeletedThenReingestedUuidUpdatesMatchingDescriptorNotStaleSlot)
{
    const auto stale     = MakeTexture();
    const auto previous  = MakeTexture();
    const auto candidate = MakeTexture();
    const auto uuid      = Register(stale);
    AssetManager::ReleaseTexture(uuid);
    AssetManager::FlushTextureReleases();
    auto* assets           = AssetManager::Instance();
    auto& descriptor       = assets->Textures.push_use({});
    descriptor.TextureUUID = uuid;
    descriptor.Handle      = previous;
    assets->UUIDToTextureHandle.insert(uuid, previous);
    ASSERT_TRUE(RRMDecodeTestHelper::StageReplacement(*Manager, uuid, previous, candidate));
    RRMDecodeTestHelper::CompleteReplacement(*Manager, candidate, true);
    RRMDecodeTestHelper::PollReplacements(*Manager);
    EXPECT_EQ(AssetManager::FindTextureHandle(uuid).Index, candidate.Index);
    EXPECT_EQ(assets->Textures[0].Handle.Index, stale.Index);
    EXPECT_EQ(assets->Textures[1].Handle.Index, candidate.Index);
}

TEST_F(TextureReplacementTest, DeletingFallbackUuidNeverQueuesGlobalFallbackForDestruction)
{
    const auto fallback                             = MakeTexture();
    AssetManager::Instance()->FallbackTextureHandle = fallback;
    const auto uuid                                 = Register(fallback);
    Manager->ReleaseTexture(uuid);
    EXPECT_FALSE(RRMDecodeTestHelper::ReleaseQueued(*Manager, fallback));
    AssetManager::FlushTextureReleases();
    EXPECT_FALSE(AssetManager::FindTextureHandle(uuid).Valid());
}

TEST(AsyncUploadCancellationTest, ClearAndCapacityRejectionReportEveryUnsubmittedJobExactlyOnce)
{
    Hardwares::AsyncUploadQueue queue    = {};
    uint32_t                    rejected = 0;
    Hardwares::AsyncUploadJob   job      = {};
    job.SubmissionContext                = &rejected;
    job.OnRejected                       = [](void* context, const Hardwares::StreamingUploadTicket&) { ++*static_cast<uint32_t*>(context); };
    for (uint32_t index = 0; index <= Hardwares::AsyncUploadQueue::CAPACITY; ++index)
    {
        queue.Enqueue(job);
    }
    // SPSCQueue reserves one sentinel slot, so usable capacity is CAPACITY - 1.
    EXPECT_EQ(rejected, 2u);
    queue.Clear();
    EXPECT_EQ(rejected, Hardwares::AsyncUploadQueue::CAPACITY + 1);
    queue.Clear();
    EXPECT_EQ(rejected, Hardwares::AsyncUploadQueue::CAPACITY + 1);
}

class ThumbnailCacheTest : public ::testing::Test
{
protected:
    std::filesystem::path Directory{};

    void                  SetUp() override
    {
        Directory = std::filesystem::temp_directory_path() / ("zengine_thumbnail_cache_" + uuids::to_string(MakeUUID()));
        ASSERT_TRUE(std::filesystem::create_directory(Directory));
    }

    void TearDown() override
    {
        std::error_code error{};
        std::filesystem::remove_all(Directory, error);
    }
};

TEST_F(ThumbnailCacheTest, WritePreservesPixelsAndRoundTripsTheirOrientation)
{
    uint8_t    pixels[] = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 255, 0};
    const auto original = std::vector<uint8_t>(std::begin(pixels), std::end(pixels));
    const auto cache    = Directory / "preview.png";
    ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(cache, pixels, 2, 2));
    EXPECT_TRUE(std::equal(original.begin(), original.end(), pixels));

    stbi_set_flip_vertically_on_load_thread(1);
    int   width = 0, height = 0, channels = 0;
    auto* decoded = stbi_load(cache.string().c_str(), &width, &height, &channels, 4);
    stbi_set_flip_vertically_on_load_thread(0);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(width, 2);
    EXPECT_EQ(height, 2);
    EXPECT_TRUE(std::equal(original.begin(), original.end(), decoded));
    stbi_image_free(decoded);
}

TEST_F(ThumbnailCacheTest, RenderedReadbackRowsRoundTripWithoutVerticalFlipOrColorConversion)
{
    uint8_t                                             pixels[] = {255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 255, 255, 255, 0};
    const auto                                          original = std::vector<uint8_t>(std::begin(pixels), std::end(pixels));
    const auto                                          path     = Directory / "rendered.png";
    const RenderResourceManager::ThumbnailCacheIdentity key      = {.SourceHash = 123, .DependencyHash = 456, .RendererRevision = 1};
    ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(path, pixels, 2, 2, key, false));
    stbi_set_flip_vertically_on_load_thread(0);
    int   width = 0, height = 0, channels = 0;
    auto* decoded = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_TRUE(std::equal(original.begin(), original.end(), decoded));
    EXPECT_TRUE(std::equal(original.begin(), original.end(), pixels));
    stbi_image_free(decoded);
    EXPECT_TRUE(RRMDecodeTestHelper::ReadCache(path, 2, 2, key));
}

TEST_F(ThumbnailCacheTest, ReadbackTicketsAreBoundedAndCancellationReclaimsEverySlot)
{
    auto resources = std::make_unique<RenderResourceManager>();
    EXPECT_EQ(resources->ReserveThumbnailCacheWrite("preview.png", {}), nullptr);
    RRMDecodeTestHelper::AcceptWork(*resources);
    void* tickets[4] = {};
    for (auto& ticket : tickets)
    {
        ticket = resources->ReserveThumbnailCacheWrite("preview.png", {});
        ASSERT_NE(ticket, nullptr);
    }
    EXPECT_EQ(resources->ReserveThumbnailCacheWrite("overflow.png", {}), nullptr);
    RenderResourceManager::CancelThumbnailCacheReadback(tickets[1]);
    EXPECT_EQ(resources->ReserveThumbnailCacheWrite("replacement.png", {}), tickets[1]);
    for (auto* ticket : tickets)
        RenderResourceManager::CancelThumbnailCacheReadback(ticket);
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*resources), 0u);
    RRMDecodeTestHelper::AcceptWork(*resources, false);
    EXPECT_EQ(resources->ReserveThumbnailCacheWrite("shutdown.png", {}), nullptr);
}

TEST_F(ThumbnailCacheTest, UnacceptedInvalidAndShutdownReadbacksNeverScheduleWrites)
{
    auto resources = std::make_unique<RenderResourceManager>();
    RRMDecodeTestHelper::AcceptWork(*resources);
    const auto                         path   = (Directory / "invalid.png").string();
    std::array<uint8_t, 128 * 128 * 4> pixels = {};
    void*                              ticket = resources->ReserveThumbnailCacheWrite(path.c_str(), {});
    ASSERT_NE(ticket, nullptr);
    RenderResourceManager::CompleteThumbnailCacheReadback(pixels.data(), pixels.size(), ticket);
    EXPECT_FALSE(RRMDecodeTestHelper::Reserved(ticket));
    ticket = resources->ReserveThumbnailCacheWrite(path.c_str(), {});
    resources->AcceptThumbnailCacheWrite(ticket);
    RenderResourceManager::CompleteThumbnailCacheReadback(pixels.data(), 4, ticket);
    EXPECT_FALSE(RRMDecodeTestHelper::Reserved(ticket));
    ticket = resources->ReserveThumbnailCacheWrite(path.c_str(), {});
    resources->AcceptThumbnailCacheWrite(ticket);
    RRMDecodeTestHelper::AcceptWork(*resources, false);
    RenderResourceManager::CompleteThumbnailCacheReadback(pixels.data(), pixels.size(), ticket);
    EXPECT_FALSE(RRMDecodeTestHelper::Reserved(ticket));
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*resources), 0u);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(ThumbnailCacheTest, CompletedReadbackCopiesPixelsAndPublishesOnWorker)
{
    auto resources = std::make_unique<RenderResourceManager>();
    RRMDecodeTestHelper::AcceptWork(*resources);
    const auto                                          path   = Directory / "worker.png";
    const auto                                          native = path.string();
    const RenderResourceManager::ThumbnailCacheIdentity key    = {.SourceHash = 12, .DependencyHash = 34, .RendererRevision = 1};
    std::array<uint8_t, 128 * 128 * 4>                  pixels{};
    pixels.fill(255);
    pixels[0]    = 37;
    void* ticket = resources->ReserveThumbnailCacheWrite(native.c_str(), key);
    ASSERT_NE(ticket, nullptr);
    resources->AcceptThumbnailCacheWrite(ticket);
    RenderResourceManager::CompleteThumbnailCacheReadback(pixels.data(), pixels.size(), ticket);
    pixels.fill(0); // The GPU readback memory is invalid immediately after delivery.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (RRMDecodeTestHelper::Pending(*resources) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    ASSERT_EQ(RRMDecodeTestHelper::Pending(*resources), 0u);
    EXPECT_FALSE(RRMDecodeTestHelper::Reserved(ticket));
    ASSERT_TRUE(RRMDecodeTestHelper::ReadCache(path, 128, 128, key));
    stbi_set_flip_vertically_on_load_thread(0);
    int   width = 0, height = 0, channels = 0;
    auto* decoded = stbi_load(native.c_str(), &width, &height, &channels, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decoded[0], 37);
    EXPECT_EQ(decoded[1], 255);
    EXPECT_EQ(decoded[3], 255);
    stbi_image_free(decoded);
}

TEST_F(ThumbnailCacheTest, SaturatedWorkerQueuesRejectPersistenceWithoutInlineIoOrDeadlock)
{
    SaturatedBackgroundPool background{};
    ASSERT_TRUE(background.Fill());
    ASSERT_FALSE(Helpers::ThreadPoolHelper::SubmitToWorker(0, nullptr, +[](void*) {}));
    auto resources = std::make_unique<RenderResourceManager>();
    RRMDecodeTestHelper::AcceptWork(*resources);
    const auto path   = (Directory / "saturated.png").string();
    void*      ticket = resources->ReserveThumbnailCacheWrite(path.c_str(), {});
    ASSERT_NE(ticket, nullptr);
    resources->AcceptThumbnailCacheWrite(ticket);
    std::array<uint8_t, 128 * 128 * 4> pixels = {};
    RenderResourceManager::CompleteThumbnailCacheReadback(pixels.data(), pixels.size(), ticket);
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*resources), 0u);
    EXPECT_FALSE(RRMDecodeTestHelper::Reserved(ticket));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST_F(ThumbnailCacheTest, ShutdownCancelsUnstartedWritesAfterWorkerPoolWasJoined)
{
    auto resources = std::make_unique<RenderResourceManager>();
    RRMDecodeTestHelper::AcceptWork(*resources);
    void* ticket = resources->ReserveThumbnailCacheWrite("not-written.png", {});
    ASSERT_NE(ticket, nullptr);
    RRMDecodeTestHelper::SimulateDroppedWorker(*resources, ticket);
    // A live pool may still deliver the queued context, so cancellation cannot
    // reclaim it until Engine has joined and destroyed that pool.
    RRMDecodeTestHelper::CancelStoppedWrites(*resources);
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*resources), 1u);
    auto previous = std::move(Helpers::ThreadPoolHelper::Pool);
    RRMDecodeTestHelper::CancelStoppedWrites(*resources);
    RRMDecodeTestHelper::CancelStoppedWrites(*resources);
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*resources), 0u);
    EXPECT_FALSE(RRMDecodeTestHelper::Reserved(ticket));
    Helpers::ThreadPoolHelper::Pool = std::move(previous);
}

TEST_F(ThumbnailCacheTest, MaintenanceRunsOnWorkerWithCopiedRootAndPreservesCurrentRegistryKey)
{
    struct PoolGuard
    {
        bool Owned = !Helpers::ThreadPoolHelper::IsInitialized();
        PoolGuard()
        {
            Helpers::ThreadPoolHelper::Initialize();
        }
        ~PoolGuard()
        {
            if (Owned)
                Helpers::ThreadPoolHelper::Shutdown();
        }
    } pool{};
    MemoryManager memory{};
    memory.Initialize(ZMega(64), {});
    AssetRegistry registry{};
    registry.Initialize(&memory.MainArena);
    const auto   uuid = MakeUUID();
    MetaFileData meta{};
    meta.AssetUUID  = uuid;
    meta.SourceHash = 0x123;
    ASSERT_TRUE(registry.Register(uuid, AssetType::TEXTURE, VFSPath::Parse("/texture.png").Value(), meta).IsOk());
    const auto root  = Directory / "project";
    const auto cache = root / ".zodiacengine/cache/thumbnails" / uuids::to_string(uuid);
    ASSERT_TRUE(std::filesystem::create_directories(cache));
    const auto current  = cache / "0000000000000123-3.png";
    const auto obsolete = cache / "0000000000000122-3.png";
    for (const auto& path : {current, obsolete})
    {
        std::ofstream(path) << "derived cache fixture";
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));
    }
    auto resources = std::make_unique<RenderResourceManager>();
    RRMDecodeTestHelper::AcceptWork(*resources);
    RRMDecodeTestHelper::SetRegistry(*resources, &registry);
    char copied[MAX_FILE_PATH_COUNT] = {};
    Helpers::secure_strcpy(copied, sizeof(copied), root.string().c_str());
    ASSERT_TRUE(resources->SubmitThumbnailCacheCleanup(copied, 3, 1));
    copied[0]           = '\0';
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (RRMDecodeTestHelper::Pending(*resources) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    ASSERT_EQ(RRMDecodeTestHelper::Pending(*resources), 0u);
    const auto stats = resources->GetThumbnailMaintenanceStatistics();
    EXPECT_FALSE(stats.Pending);
    EXPECT_EQ(stats.Runs, 1u);
    EXPECT_EQ(stats.Removed, 1u);
    EXPECT_EQ(stats.Errors, 0u);
    EXPECT_TRUE(std::filesystem::exists(current));
    EXPECT_FALSE(std::filesystem::exists(obsolete));
    RRMDecodeTestHelper::AcceptWork(*resources, false);
    EXPECT_FALSE(resources->SubmitThumbnailCacheCleanup(root.string().c_str(), 3, 1));
    memory.Shutdown();
}

TEST_F(ThumbnailCacheTest, StoppedPoolCancelsUnstartedMaintenanceWithoutLeakingShutdownCounter)
{
    auto resources = std::make_unique<RenderResourceManager>();
    RRMDecodeTestHelper::SimulateDroppedCleanup(*resources);
    auto previous = std::move(Helpers::ThreadPoolHelper::Pool);
    RRMDecodeTestHelper::CancelStoppedWrites(*resources);
    RRMDecodeTestHelper::CancelStoppedWrites(*resources);
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*resources), 0u);
    EXPECT_FALSE(resources->GetThumbnailMaintenanceStatistics().Pending);
    Helpers::ThreadPoolHelper::Pool = std::move(previous);
}

TEST_F(ThumbnailCacheTest, MaterialPreviewReadinessIncludesQueuedReloadsAndActiveDecodes)
{
    auto resources = std::make_unique<RenderResourceManager>();
    EXPECT_FALSE(resources->HasPendingTextureUploads());
    uint8_t slab = UINT8_MAX;
    ASSERT_TRUE(RRMDecodeTestHelper::Acquire(*resources, &slab));
    EXPECT_TRUE(resources->HasPendingTextureUploads());
    RRMDecodeTestHelper::Release(*resources, slab);
    EXPECT_FALSE(resources->HasPendingTextureUploads());
    RRMDecodeTestHelper::QueueUnsubmittedUpload(*resources);
    EXPECT_TRUE(resources->HasPendingTextureUploads());
    RRMDecodeTestHelper::ClearUnsubmittedUploads(*resources);
    EXPECT_FALSE(resources->HasPendingTextureUploads());
    auto device = std::make_unique<Hardwares::VulkanDevice>();
    RRMDecodeTestHelper::SetDevice(*resources, device.get());
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 1};
    device->RequestDeferredDescriptorUpdate(texture);
    EXPECT_TRUE(resources->HasPendingTextureUploads());
    Textures::TextureHandle discarded = {};
    EXPECT_TRUE(device->DeferredTextureDescriptorUpdates.pop(discarded));
    device->RequestDescriptorUpdate(texture);
    EXPECT_TRUE(resources->HasPendingTextureUploads());
    device->TextureHandleToUpdates.clear();
    EXPECT_FALSE(resources->HasPendingTextureUploads());
    RRMDecodeTestHelper::SetDevice(*resources, nullptr);
    resources->ScheduleTextureReload(MakeUUID());
    EXPECT_TRUE(resources->HasPendingTextureUploads());
}

TEST_F(ThumbnailCacheTest, WriterDoesNotClobberAnotherWritersTemporaryFile)
{
    const auto cache     = Directory / "preview.png";
    const auto temporary = Directory / "preview.png.tmp";
    {
        std::ofstream output(temporary, std::ios::binary);
        output << "owned by another writer";
    }
    uint8_t pixels[] = {80, 120, 160, 255};
    ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(cache, pixels, 1, 1));
    std::ifstream input(temporary, std::ios::binary);
    std::string   contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "owned by another writer");
}

TEST_F(ThumbnailCacheTest, FailedWritePreservesPixelsAndLeavesNoTemporaryFile)
{
    const auto parent_file = Directory / "not-a-directory";
    {
        std::ofstream output(parent_file);
        output << "source";
    }
    uint8_t    pixels[] = {80, 120, 160, 255, 1, 2, 3, 4};
    const auto original = std::vector<uint8_t>(std::begin(pixels), std::end(pixels));
    EXPECT_FALSE(RRMDecodeTestHelper::WriteCache(parent_file / "preview.png", pixels, 1, 2));
    EXPECT_TRUE(std::equal(original.begin(), original.end(), pixels));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(Directory), std::filesystem::directory_iterator()), 1);
}

TEST_F(ThumbnailCacheTest, ConcurrentWritersPublishACompleteImageWithoutOrphanedTemporaries)
{
    const auto         cache                 = Directory / "preview.png";
    constexpr uint32_t extent                = 32;
    constexpr uint32_t writer_count          = 4;
    bool               results[writer_count] = {};
    std::thread        writers[writer_count]{};
    for (uint32_t index = 0; index < writer_count; ++index)
    {
        writers[index] = std::thread([&, index] {
            std::vector<uint8_t> pixels(extent * extent * 4, static_cast<uint8_t>(20 + index));
            results[index] = RRMDecodeTestHelper::WriteCache(cache, pixels.data(), extent, extent);
        });
    }
    for (auto& writer : writers)
        writer.join();
    EXPECT_TRUE(std::any_of(std::begin(results), std::end(results), [](bool result) { return result; }));
    int   width = 0, height = 0, channels = 0;
    auto* decoded = stbi_load(cache.string().c_str(), &width, &height, &channels, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(width, extent);
    EXPECT_EQ(height, extent);
    EXPECT_GE(decoded[0], 20);
    EXPECT_LT(decoded[0], 20 + writer_count);
    EXPECT_TRUE(std::all_of(decoded, decoded + width * height * 4, [value = decoded[0]](uint8_t pixel) { return pixel == value; }));
    stbi_image_free(decoded);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(Directory), std::filesystem::directory_iterator()), 1);
}

TEST_F(ThumbnailCacheTest, InvalidInputsNeverCreateCacheFiles)
{
    uint8_t    pixels[] = {1, 2, 3, 4};
    const auto cache    = Directory / "preview.png";
    EXPECT_FALSE(RRMDecodeTestHelper::WriteCache(cache, nullptr, 1, 1));
    EXPECT_FALSE(RRMDecodeTestHelper::WriteCache(cache, pixels, 0, 1));
    EXPECT_FALSE(RRMDecodeTestHelper::WriteCache(cache, pixels, 1, 0));
    EXPECT_FALSE(RRMDecodeTestHelper::WriteCache(cache, pixels, 513, 1));
    EXPECT_FALSE(RRMDecodeTestHelper::WriteCache(cache, pixels, 1, 513));
    EXPECT_TRUE(std::filesystem::is_empty(Directory));
}

TEST_F(ThumbnailCacheTest, ValidatesFullIdentityDimensionsAndEncoding)
{
    const auto                                    path       = Directory / "preview.png";
    uint8_t                                       pixels[16] = {255, 0, 0, 255};
    RenderResourceManager::ThumbnailCacheIdentity key        = {.AssetUUID = MakeUUID(), .SourceHash = 123, .DependencyHash = 456, .SchemaRevision = 3, .RendererRevision = 7};
    ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(path, pixels, 2, 2, key));
    EXPECT_TRUE(RRMDecodeTestHelper::ReadCache(path, 2, 2, key));
    auto changed      = key;
    changed.AssetUUID = MakeUUID();
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 2, changed));
    changed = key;
    ++changed.SourceHash;
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 2, changed));
    changed = key;
    ++changed.DependencyHash;
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 2, changed));
    changed = key;
    ++changed.SchemaRevision;
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 2, changed));
    changed = key;
    ++changed.RendererRevision;
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 2, changed));
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 1, 2, key));
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 1, key));
}

TEST_F(ThumbnailCacheTest, RejectsPayloadAndMetadataCorruptionAndTruncation)
{
    const auto path       = Directory / "preview.png";
    uint8_t    pixels[16] = {255, 0, 0, 255};
    ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(path, pixels, 2, 2));
    const size_t length    = std::filesystem::file_size(path);
    // Corrupt PNG payload, color space, encoder, byte count and checksum.
    const size_t offsets[] = {40, length - 80 + 56, length - 80 + 60, length - 80 + 64, length - 1};
    for (const size_t offset : offsets)
    {
        ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(path, pixels, 2, 2));
        {
            std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
            file.seekg(static_cast<std::streamoff>(offset));
            char value = 0;
            file.read(&value, 1);
            value ^= 0x40;
            file.seekp(static_cast<std::streamoff>(offset));
            file.write(&value, 1);
        }
        EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 2)) << offset;
    }
    for (const size_t size : {size_t(0), size_t(79), length - 1})
    {
        ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(path, pixels, 2, 2));
        std::filesystem::resize_file(path, size);
        EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 2, 2));
    }
    ASSERT_TRUE(RRMDecodeTestHelper::WriteCache(path, pixels, 2, 2));
    EXPECT_TRUE(RRMDecodeTestHelper::ReadCache(path, 2, 2));
}

TEST_F(ThumbnailCacheTest, RejectsOversizedFilesAndMissingMetadataWithoutDecode)
{
    const auto path = Directory / "preview.png";
    {
        std::ofstream file(path);
        file << "not a preview";
    }
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 128, 128));
    std::filesystem::resize_file(path, 3 * 1024 * 1024);
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(path, 128, 128));
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(Directory / "missing.png", 128, 128));
    EXPECT_FALSE(RRMDecodeTestHelper::ReadCache(Directory, 128, 128));
}

// ============================================================
// RenderHandle tests — no GPU, no device
// ============================================================

TEST(RenderHandle, DefaultConstructedIsInvalid)
{
    BufferHandle h;
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.Generation, 0u);
}

TEST(RenderHandle, NonZeroGenerationIsValid)
{
    BufferHandle h{0, 1};
    EXPECT_TRUE(h.IsValid());
}

TEST(RenderHandle, EqualityRequiresBothFieldsToMatch)
{
    BufferHandle a{3, 7};
    BufferHandle b{3, 7};
    BufferHandle c{3, 8}; // same index, different generation
    BufferHandle d{4, 7}; // different index, same generation

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(RenderHandle, DifferentTagTypesAreIncompatible)
{
    // Compile-time check: BufferHandle and SamplerHandle are distinct types.
    static_assert(!std::is_same_v<BufferHandle, SamplerHandle>, "BufferHandle and SamplerHandle must be distinct types");
    static_assert(!std::is_same_v<SamplerHandle, PipelineHandle>, "SamplerHandle and PipelineHandle must be distinct types");

    // Runtime sanity: same {index, generation} in different handle types are not interchangeable.
    BufferHandle  buf{1, 2};
    SamplerHandle smp{1, 2};
    EXPECT_EQ(buf.Index, smp.Index);
    EXPECT_EQ(buf.Generation, smp.Generation);
    // They cannot be compared (different types) — confirmed by static_assert above.
    SUCCEED();
}

TEST(RenderHandle, TriviallyCopiable)
{
    static_assert(std::is_trivially_copyable_v<BufferHandle>, "BufferHandle must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<SamplerHandle>, "SamplerHandle must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<PipelineHandle>, "PipelineHandle must be trivially copyable");
    SUCCEED();
}

TEST(RenderHandle, SizeFitsInEightBytes)
{
    static_assert(sizeof(BufferHandle) == 8, "BufferHandle must fit in 8 bytes");
    static_assert(sizeof(SamplerHandle) == 8, "SamplerHandle must fit in 8 bytes");
    static_assert(sizeof(PipelineHandle) == 8, "PipelineHandle must fit in 8 bytes");
    SUCCEED();
}

TEST(RenderResourceManagerDecodePolicy, LeasesAreBoundedAndReclaimed)
{
    // RenderResourceManager contains bounded queues and slot tables, making it
    // too large for the default Windows test-thread stack.
    auto    manager                                 = std::make_unique<RenderResourceManager>();
    uint8_t leases[RRMDecodeTestHelper::Capacity()] = {};

    for (uint32_t index = 0; index < RRMDecodeTestHelper::Capacity(); ++index)
    {
        EXPECT_TRUE(RRMDecodeTestHelper::Acquire(*manager, &leases[index]));
        EXPECT_EQ(leases[index], index);
    }

    uint8_t unavailable = UINT8_MAX;
    EXPECT_FALSE(RRMDecodeTestHelper::Acquire(*manager, &unavailable));

    RRMDecodeTestHelper::Release(*manager, leases[2]);
    uint8_t reclaimed = UINT8_MAX;
    EXPECT_TRUE(RRMDecodeTestHelper::Acquire(*manager, &reclaimed));
    EXPECT_EQ(reclaimed, leases[2]);

    for (uint32_t index = 0; index < RRMDecodeTestHelper::Capacity(); ++index)
        if (index != 2)
            RRMDecodeTestHelper::Release(*manager, leases[index]);
    RRMDecodeTestHelper::Release(*manager, reclaimed);
}

TEST(RenderResourceManagerDecodePolicy, TrackedTextureStaysPendingUntilItsOwnUploadIsSubmitted)
{
    auto                          manager = std::make_unique<RenderResourceManager>();
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*manager, texture));
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Pending);

    RRMDecodeTestHelper::Submitted(*manager, {.Index = 7, .Generation = 4});
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Pending);
    RRMDecodeTestHelper::Submitted(*manager, texture);
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Succeeded);

    manager->ForgetTextureDecode(texture);
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Untracked);
}

TEST(RenderResourceManagerDecodePolicy, PendingReleaseIsDeduplicatedAndDoesNotForgetTheDecode)
{
    auto                          manager = std::make_unique<RenderResourceManager>();
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    ASSERT_TRUE(RRMDecodeTestHelper::Track(*manager, texture));
    RRMDecodeTestHelper::ReleaseLater(*manager, texture);
    RRMDecodeTestHelper::ReleaseLater(*manager, texture);

    // No device is needed: pending CPU work must never reach GPU destruction.
    RRMDecodeTestHelper::FlushReleases(*manager);
    EXPECT_EQ(RRMDecodeTestHelper::ReleaseCount(*manager), 1u);
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Pending);
}

TEST_F(ThumbnailCacheTest, FourKTextureDecodeFitsOneLeaseAndReleasesItForReuse)
{
    const auto           source = Directory / "four-k.jpg";
    std::vector<uint8_t> pixels(4096 * 4096 * 3, 120);
    ASSERT_NE(stbi_write_jpg(source.string().c_str(), 4096, 4096, 3, pixels.data(), 80), 0);
    pixels.clear();
    pixels.shrink_to_fit();

    MemoryManager memory;
    memory.Initialize(ZMega(136), {});
    auto manager = std::make_unique<RenderResourceManager>();
    auto device  = std::make_unique<Hardwares::VulkanDevice>();
    RRMDecodeTestHelper::InitDecode(*manager, memory.MainArena, *device);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        EXPECT_TRUE(RRMDecodeTestHelper::Decode(*manager, source.string().c_str(), 4096));
        EXPECT_EQ(RRMDecodeTestHelper::Pending(*manager), 0u);
    }
    // A larger requested output cannot fit alongside the source. Fail without
    // asserting, then prove the lease and source allocation were reclaimed.
    EXPECT_FALSE(RRMDecodeTestHelper::Decode(*manager, source.string().c_str(), 8192));
    EXPECT_TRUE(RRMDecodeTestHelper::Decode(*manager, source.string().c_str(), 128, true));
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*manager), 0u);
    EXPECT_EQ(Helpers::GetWorkerSlab(), nullptr);
    RRMDecodeTestHelper::ShutdownDecode(*manager);
}

TEST_F(ThumbnailCacheTest, DescriptorAdmissionFailureKeepsUploadOwnershipAndCannotBecomeSuccess)
{
    const auto    source   = Directory / "descriptor-backpressure.png";
    const uint8_t pixels[] = {80, 120, 160, 255};
    ASSERT_NE(stbi_write_png(source.string().c_str(), 1, 1, 4, pixels, 4), 0);
    MemoryManager memory = {};
    memory.Initialize(ZMega(136), {});
    auto manager = std::make_unique<RenderResourceManager>();
    auto device  = std::make_unique<Hardwares::VulkanDevice>();
    RRMDecodeTestHelper::InitDecode(*manager, memory.MainArena, *device);
    for (uint32_t index = 0; index < decltype(device->DeferredTextureDescriptorUpdates)::CAPACITY; ++index)
    {
        ASSERT_TRUE(device->RequestDeferredDescriptorUpdate({.Index = index, .Generation = 2}));
    }
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 4};
    ASSERT_TRUE(RRMDecodeTestHelper::SubmitDecode(*manager, source.string().c_str(), 4, false, texture));
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*manager), 0u);
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    // Rejection cannot return the slab while the upload queue still owns pixels.
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*manager), 1u);
    RRMDecodeTestHelper::Submitted(*manager, texture);
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    RRMDecodeTestHelper::DiscardDeferrals(*manager);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*manager), 0u);
    Textures::TextureHandle discarded = {};
    while (device->DeferredTextureDescriptorUpdates.pop(discarded))
    {
    }
    // Normal decoding still reuses the same slab after failed-work cleanup.
    EXPECT_TRUE(RRMDecodeTestHelper::Decode(*manager, source.string().c_str(), 4));
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*manager), 0u);
    RRMDecodeTestHelper::ShutdownDecode(*manager);
}

TEST_F(ThumbnailCacheTest, FullDeferralQueueReclaimsUntransferredPixelsAndReusesTheDecodeLease)
{
    const auto    source   = Directory / "deferral-backpressure.png";
    const uint8_t pixels[] = {80, 120, 160, 255};
    ASSERT_NE(stbi_write_png(source.string().c_str(), 1, 1, 4, pixels, 4), 0);
    MemoryManager memory = {};
    memory.Initialize(ZMega(136), {});
    auto manager = std::make_unique<RenderResourceManager>();
    auto device  = std::make_unique<Hardwares::VulkanDevice>();
    RRMDecodeTestHelper::InitDecode(*manager, memory.MainArena, *device);
    ASSERT_GT(RRMDecodeTestHelper::FillDeferralQueue(*manager), 0u);
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 4};
    EXPECT_TRUE(RRMDecodeTestHelper::SubmitDecode(*manager, source.string().c_str(), 4, false, texture));
    EXPECT_EQ(RRMDecodeTestHelper::Pending(*manager), 0u);
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*manager), 0u);
    EXPECT_EQ(manager->GetTextureDecodeState(texture), RenderResourceManager::TextureDecodeState::Failed);
    EXPECT_TRUE(device->DeferredTextureDescriptorUpdates.empty());
    RRMDecodeTestHelper::DiscardDeferrals(*manager);
    EXPECT_TRUE(RRMDecodeTestHelper::Decode(*manager, source.string().c_str(), 4));
    EXPECT_EQ(RRMDecodeTestHelper::ActiveDecodeSlabs(*manager), 0u);
    EXPECT_EQ(Helpers::GetWorkerSlab(), nullptr);
    RRMDecodeTestHelper::ShutdownDecode(*manager);
}

TEST(RenderResourceManagerThumbnailPolicy, AspectFitClearsPaddingAndPreservesAlpha)
{
    uint8_t source[4 * 2 * 4] = {};
    for (uint32_t pixel = 0; pixel < 8; ++pixel)
    {
        source[pixel * 4]     = 80;
        source[pixel * 4 + 1] = 120;
        source[pixel * 4 + 2] = 160;
        source[pixel * 4 + 3] = 192;
    }

    for (const bool portrait : {false, true})
    {
        uint8_t destination[8 * 8 * 4]{};
        std::fill(std::begin(destination), std::end(destination), 0xcd);
        ASSERT_TRUE(RRMDecodeTestHelper::Fit(source, portrait ? 2 : 4, portrait ? 4 : 2, destination, 8));
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; ++x)
            {
                const bool  padding = portrait ? (x < 2 || x >= 6) : (y < 2 || y >= 6);
                const auto* pixel   = destination + (y * 8 + x) * 4;
                EXPECT_EQ(pixel[0], padding ? 0 : 80);
                EXPECT_EQ(pixel[1], padding ? 0 : 120);
                EXPECT_EQ(pixel[2], padding ? 0 : 160);
                EXPECT_EQ(pixel[3], padding ? 0 : 192);
            }
    }
}

TEST(RenderResourceManagerThumbnailPolicy, InvalidInputDoesNotTouchTheDestination)
{
    const uint8_t source[4]      = {80, 120, 160, 192};
    uint8_t       destination[4] = {1, 2, 3, 4};
    EXPECT_FALSE(RRMDecodeTestHelper::Fit(source, 0, 1, destination, 1));
    EXPECT_FALSE(RRMDecodeTestHelper::Fit(source, 1, -1, destination, 1));
    EXPECT_FALSE(RRMDecodeTestHelper::Fit(source, 1, 1, destination, 513));
    EXPECT_FALSE(RRMDecodeTestHelper::Fit(nullptr, 1, 1, destination, 1));
    EXPECT_EQ(destination[0], 1);
    EXPECT_EQ(destination[3], 4);
}

// ============================================================
// AssetRegistry OnReady / OnStale callback wiring
// ============================================================

class AssetRegistryCallbackTest : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    AssetRegistry m_registry;

    void          SetUp() override
    {
        // AssetIndex pre-allocates 8192 AssetRecord slots (~800 bytes each = ~6.5 MB)
        // plus UUID/path hashmaps. 32 MB gives comfortable headroom.
        m_manager.Initialize(ZMega(32), {});
        m_registry.Initialize(&m_manager.MainArena);
    }

    void TearDown() override
    {
        m_manager.Shutdown();
    }
};

TEST_F(AssetRegistryCallbackTest, OnReadyFiredWhenStateTransitionsToLoaded)
{
    uuids::uuid fired_uuid;
    AssetHandle fired_handle = 0;
    int         call_count   = 0;

    m_registry.SetOnReadyCallback(nullptr, [](void*, const uuids::uuid& uuid, AssetHandle handle) {
        // Can't capture, use global for test — stash via out-params via ctx trick below
        (void) uuid;
        (void) handle;
    });

    // Use ctx pointer to capture results
    struct Ctx
    {
        uuids::uuid uuid;
        AssetHandle handle;
        int         count;
    };
    Ctx ctx{};
    m_registry.SetOnReadyCallback(&ctx, [](void* raw, const uuids::uuid& uuid, AssetHandle handle) {
        auto* c   = static_cast<Ctx*>(raw);
        c->uuid   = uuid;
        c->handle = handle;
        c->count++;
    });

    // Register an asset
    uuids::uuid             test_uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID          = test_uuid;

    // Register as Loaded (simulates IngestMesh path)
    AssetHandle slot_handle = 0xDEAD;
    m_registry.RegisterLoaded(test_uuid, AssetType::MESH, {}, meta, slot_handle);

    // RegisterLoaded sets state directly — callback fires only through SetState.
    // Call SetState explicitly as IngestMesh does.
    m_registry.SetState(test_uuid, AssetState::Loaded);

    EXPECT_EQ(ctx.count, 1);
    EXPECT_EQ(ctx.uuid, test_uuid);
    EXPECT_EQ(ctx.handle, slot_handle);
}

TEST_F(AssetRegistryCallbackTest, OnReadyNotFiredForNonLoadedTransitions)
{
    struct Ctx
    {
        int count = 0;
    };
    Ctx ctx{};

    m_registry.SetOnReadyCallback(&ctx, [](void* raw, const uuids::uuid&, AssetHandle) { static_cast<Ctx*>(raw)->count++; });

    uuids::uuid             test_uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID = test_uuid;
    m_registry.Register(test_uuid, AssetType::MESH, {}, meta);

    m_registry.SetState(test_uuid, AssetState::Importing);
    m_registry.SetState(test_uuid, AssetState::Failed);

    EXPECT_EQ(ctx.count, 0);
}

TEST_F(AssetRegistryCallbackTest, OnReadyFiresIndependentlyOfHotReloadCallback)
{
    struct Ctx
    {
        int ready_count  = 0;
        int reload_count = 0;
    };
    Ctx ctx{};

    m_registry.SetOnReadyCallback(&ctx, [](void* raw, const uuids::uuid&, AssetHandle) { static_cast<Ctx*>(raw)->ready_count++; });
    m_registry.SetHotReloadCallback(&ctx, [](void* raw, std::span<const uuids::uuid>) { static_cast<Ctx*>(raw)->reload_count++; });

    uuids::uuid             uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID = uuid;
    m_registry.RegisterLoaded(uuid, AssetType::MESH, {}, meta, 1u);
    m_registry.SetState(uuid, AssetState::Loaded);

    EXPECT_EQ(ctx.ready_count, 1) << "OnReady must fire once";
    EXPECT_EQ(ctx.reload_count, 1) << "HotReload callback must also fire";
}

TEST_F(AssetRegistryCallbackTest, OnStaleFiredByOnAssetModified)
{
    struct Ctx
    {
        uuids::uuid uuid;
        int         count = 0;
    };
    Ctx ctx{};

    m_registry.SetOnStaleCallback(&ctx, [](void* raw, const uuids::uuid& uuid) {
        auto* c = static_cast<Ctx*>(raw);
        c->uuid = uuid;
        c->count++;
    });

    // Register with a real VFS path so OnAssetModified can find it
    uuids::uuid             uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID   = uuid;

    auto path_result = Core::VFS::VFSPath::Parse("/Assets/test.glb");
    ASSERT_TRUE(path_result.Succeeded());

    m_registry.Register(uuid, AssetType::MESH, path_result.Value(), meta);
    m_registry.OnAssetModified(path_result.Value());

    EXPECT_EQ(ctx.count, 1);
    EXPECT_EQ(ctx.uuid, uuid);
}

TEST_F(AssetRegistryCallbackTest, OnStaleNotFiredWhenNoCallback)
{
    // Verify no crash when OnStale callback is null and OnAssetModified fires.
    uuids::uuid             uuid = MakeUUID();
    Core::VFS::MetaFileData meta{};
    meta.AssetUUID   = uuid;

    auto path_result = Core::VFS::VFSPath::Parse("/Assets/nocallback.glb");
    ASSERT_TRUE(path_result.Succeeded());
    m_registry.Register(uuid, AssetType::MESH, path_result.Value(), meta);

    // No SetOnStaleCallback call — must not crash
    EXPECT_NO_FATAL_FAILURE(m_registry.OnAssetModified(path_result.Value()));
}

TEST_F(AssetRegistryCallbackTest, OnStaleFiredForEachUUIDInCascade)
{
    struct Ctx
    {
        std::vector<uuids::uuid> uuids;
    };
    Ctx ctx{};

    m_registry.SetOnStaleCallback(&ctx, [](void* raw, const uuids::uuid& uuid) { static_cast<Ctx*>(raw)->uuids.push_back(uuid); });

    // Register a mesh and a material that depends on it
    uuids::uuid             mesh_uuid = MakeUUID();
    uuids::uuid             mat_uuid  = MakeUUID();

    Core::VFS::MetaFileData mesh_meta{};
    mesh_meta.AssetUUID = mesh_uuid;
    Core::VFS::MetaFileData mat_meta{};
    mat_meta.AssetUUID = mat_uuid;

    auto mesh_path     = Core::VFS::VFSPath::Parse("/Assets/mesh.glb");
    ASSERT_TRUE(mesh_path.Succeeded());
    auto mat_path = Core::VFS::VFSPath::Parse("/Assets/mat.zematerial");
    ASSERT_TRUE(mat_path.Succeeded());

    m_registry.Register(mesh_uuid, AssetType::MESH, mesh_path.Value(), mesh_meta);
    m_registry.Register(mat_uuid, AssetType::MATERIAL, mat_path.Value(), mat_meta);
    m_registry.AddDependency(mat_uuid, mesh_uuid);

    m_registry.OnAssetModified(mesh_path.Value());

    // Both mesh and its dependent material should be stale
    EXPECT_EQ(ctx.uuids.size(), 2u);
    EXPECT_NE(std::find(ctx.uuids.begin(), ctx.uuids.end(), mesh_uuid), ctx.uuids.end());
    EXPECT_NE(std::find(ctx.uuids.begin(), ctx.uuids.end(), mat_uuid), ctx.uuids.end());
}
