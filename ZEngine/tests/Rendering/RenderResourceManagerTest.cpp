#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/RenderHandle.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <gtest/gtest.h>
#include <stb/stb_image.h>
#include <uuid.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace ZEngine;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Rendering;
using namespace ZEngine::Managers;

struct RRMDecodeTestHelper
{
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
    static void SetRegistry(RenderResourceManager& manager, AssetRegistry* registry)
    {
        manager.m_registry = registry;
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

static uuids::uuid MakeUUID()
{
    std::random_device           rd;
    std::mt19937                 gen(rd());
    uuids::uuid_random_generator g{gen};
    return g();
}

class ThumbnailCacheTest : public ::testing::Test
{
protected:
    std::filesystem::path Directory;

    void                  SetUp() override
    {
        Directory = std::filesystem::temp_directory_path() / ("zengine_thumbnail_cache_" + uuids::to_string(MakeUUID()));
        ASSERT_TRUE(std::filesystem::create_directory(Directory));
    }

    void TearDown() override
    {
        std::error_code error;
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
    std::array<uint8_t, 128 * 128 * 4>                  pixels;
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
    struct PoolGuard
    {
        Helpers::Scope<Helpers::ThreadPool> Previous = std::move(Helpers::ThreadPoolHelper::Pool);
        std::atomic<bool>                   Begun = false, Release = false;
        PoolGuard()
        {
            Helpers::ThreadPoolHelper::Pool = Helpers::CreateScope<Helpers::ThreadPool>(2);
        }
        ~PoolGuard()
        {
            Release.store(true);
            Helpers::ThreadPoolHelper::Shutdown();
            Helpers::ThreadPoolHelper::Pool = std::move(Previous);
        }
        static void Block(void* context)
        {
            auto& guard = *static_cast<PoolGuard*>(context);
            guard.Begun.store(true);
            while (!guard.Release.load())
                std::this_thread::yield();
        }
    } guard;
    auto* pool = Helpers::ThreadPoolHelper::Pool.get();
    ASSERT_TRUE(pool->SubmitToWorker(0, &guard, &PoolGuard::Block));
    while (!guard.Begun.load())
        std::this_thread::yield();
    for (uint32_t i = 0; i < Helpers::ThreadPool::MAX_TASKS_PER_WORKER; ++i)
        ASSERT_TRUE(pool->SubmitToWorker(0, nullptr, +[](void*) {}));
    ASSERT_FALSE(pool->SubmitToWorker(0, nullptr, +[](void*) {}));
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
    } pool;
    MemoryManager memory;
    memory.Initialize(ZMega(64), {});
    AssetRegistry registry;
    registry.Initialize(&memory.MainArena);
    const auto   uuid = MakeUUID();
    MetaFileData meta;
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
    std::thread        writers[writer_count];
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
        uint8_t destination[8 * 8 * 4];
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
