#include <ZEngine/Rendering/ThumbnailCacheMaintenance.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

using ZEngine::Rendering::ThumbnailCacheMaintenance;
namespace
{
    namespace fs = std::filesystem;
    class ThumbnailCacheMaintenanceTest : public ::testing::Test
    {
    protected:
        fs::path                              Root{}, Cache{}, Directory{};
        ThumbnailCacheMaintenance::CurrentKey Key{};
        ThumbnailCacheMaintenance::Resolution Resolution = ThumbnailCacheMaintenance::Resolution::Current;
        void                                  SetUp() override
        {
            Root         = fs::temp_directory_path() / ("thumbnail-cleanup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Cache        = Root / ".zodiacengine/cache/thumbnails";
            Key.Identity = {.AssetUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111"), .SourceHash = 0x123, .DependencyHash = 0x456, .SchemaRevision = 3, .RendererRevision = 1};
            Directory    = Cache / uuids::to_string(Key.Identity.AssetUUID);
            ASSERT_TRUE(fs::create_directories(Directory));
        }
        void TearDown() override
        {
            std::error_code ignored{};
            fs::remove_all(Root, ignored);
        }
        fs::path File(const char* name, std::chrono::hours age = std::chrono::hours(1))
        {
            const auto path = Directory / name;
            std::ofstream(path) << "regenerable thumbnail fixture";
            fs::last_write_time(path, fs::file_time_type::clock::now() - age);
            return path;
        }
        auto Cleanup(const std::atomic<bool>* accepting = nullptr, uint32_t renderer_revision = 1)
        {
            return ThumbnailCacheMaintenance::Cleanup(
                Root.string().c_str(),
                3,
                renderer_revision,
                +[](void* context, const uuids::uuid&, ThumbnailCacheMaintenance::CurrentKey* key) {
                    auto& fixture = *static_cast<ThumbnailCacheMaintenanceTest*>(context);
                    *key          = fixture.Key;
                    return fixture.Resolution;
                },
                this,
                accepting);
        }
    };
} // namespace

TEST_F(ThumbnailCacheMaintenanceTest, RemovesOldSourcesDependenciesFormatsAndGeneratedSidecarsButKeepsCurrent)
{
    const auto current         = File("0000000000000123-0000000000000456-3-1.png");
    const auto source          = File("0000000000000122-0000000000000456-3-1.png");
    const auto dependency      = File("0000000000000123-0000000000000455-3-1.png");
    const auto schema          = File("0000000000000123-1.png");
    const auto sidecar         = File("0000000000000123-1.png.meta");
    const auto future          = File("0000000000000123-0000000000000456-4-1.png");
    const auto future_renderer = File("0000000000000123-0000000000000456-3-2.png");
    const auto unknown         = File("keep-me.png");
    const auto before          = fs::last_write_time(current);
    const auto stats           = Cleanup();
    EXPECT_EQ(stats.Removed, 4u);
    EXPECT_GT(stats.ReclaimedBytes, 0u);
    EXPECT_EQ(stats.Errors, 0u);
    for (const auto& path : {source, dependency, schema, sidecar})
        EXPECT_FALSE(fs::exists(path));
    for (const auto& path : {current, future, future_renderer, unknown})
        EXPECT_TRUE(fs::exists(path));
    EXPECT_EQ(before, fs::last_write_time(current));
    EXPECT_EQ(Cleanup().Removed, 0u);
}

TEST_F(ThumbnailCacheMaintenanceTest, OrphansAndFullyTransparentPreviewsAreRegenerableButTransientImportsArePreserved)
{
    auto current = File("0000000000000123-0000000000000456-3-1.png");
    Resolution   = ThumbnailCacheMaintenance::Resolution::Unknown;
    EXPECT_EQ(Cleanup().Removed, 0u);
    EXPECT_TRUE(fs::exists(current));
    Key.FullyTransparent = true;
    Resolution           = ThumbnailCacheMaintenance::Resolution::Current;
    EXPECT_EQ(Cleanup().Removed, 1u);
    EXPECT_FALSE(fs::exists(Directory));
    fs::create_directory(Directory);
    current    = File("0000000000000123-0000000000000456-3-1.png");
    Resolution = ThumbnailCacheMaintenance::Resolution::Missing;
    EXPECT_EQ(Cleanup().Removed, 1u);
    EXPECT_FALSE(fs::exists(current));
}

TEST_F(ThumbnailCacheMaintenanceTest, ActiveWritesAndUnknownTemporaryNamesSurviveButDayOldCrashLeftFilesAreRemoved)
{
    const auto active    = File("0000000000000122-3.png.22222222-2222-4222-8222-222222222222.tmp");
    const auto abandoned = File("0000000000000122-3.png.33333333-3333-4333-8333-333333333333.tmp", std::chrono::hours(25));
    const auto unknown   = File("notes.txt.33333333-3333-4333-8333-333333333333.tmp", std::chrono::hours(25));
    EXPECT_EQ(Cleanup().Removed, 1u);
    EXPECT_TRUE(fs::exists(active));
    EXPECT_TRUE(fs::exists(unknown));
    EXPECT_FALSE(fs::exists(abandoned));
}

TEST_F(ThumbnailCacheMaintenanceTest, UnloadedMaterialStillPrunesOldSourcesButPreservesUnresolvedDependencyVariants)
{
    const auto source  = File("0000000000000122-0000000000000456-3-1.png");
    const auto variant = File("0000000000000123-0000000000000455-3-1.png");
    Resolution         = ThumbnailCacheMaintenance::Resolution::SourceOnly;
    EXPECT_EQ(Cleanup().Removed, 1u);
    EXPECT_FALSE(fs::exists(source));
    EXPECT_TRUE(fs::exists(variant));
}

TEST_F(ThumbnailCacheMaintenanceTest, DoesNotFollowDirectoryFileOrAncestorSymlinksOrRecurseIntoUnknownDirectories)
{
    const auto outside = Root / "source-assets";
    fs::create_directory(outside);
    const auto asset = outside / "0000000000000122-3.png";
    std::ofstream(asset) << "source asset - never delete";
    fs::create_symlink(asset, Directory / "0000000000000122-3.png");
    fs::create_directory_symlink(outside, Cache / "22222222-2222-4222-8222-222222222222");
    fs::create_directory(Directory / "nested");
    fs::copy_file(asset, Directory / "nested/0000000000000122-3.png");
    EXPECT_EQ(Cleanup().Removed, 0u);
    EXPECT_TRUE(fs::exists(asset));
    EXPECT_TRUE(fs::is_symlink(Directory / "0000000000000122-3.png"));
    EXPECT_TRUE(fs::exists(Directory / "nested/0000000000000122-3.png"));
    fs::rename(Root / ".zodiacengine", Root / "redirected");
    fs::create_directory_symlink(Root / "redirected", Root / ".zodiacengine");
    EXPECT_EQ(Cleanup().Examined, 0u);
}

TEST_F(ThumbnailCacheMaintenanceTest, CancellationAndTextureIdentityKeepWorkBoundedAndDoNotRewriteWarmFiles)
{
    Key.Identity.DependencyHash   = 0;
    Key.Identity.RendererRevision = 0;
    const auto        current     = File("0000000000000123-3.png");
    const auto        old         = File("0000000000000122-3.png");
    std::atomic<bool> accepting   = false;
    EXPECT_EQ(Cleanup(&accepting).Examined, 0u);
    EXPECT_TRUE(fs::exists(old));
    accepting.store(true);
    EXPECT_EQ(Cleanup(&accepting).Removed, 1u);
    EXPECT_TRUE(fs::exists(current));
}

TEST_F(ThumbnailCacheMaintenanceTest, AlphaRendererRevisionRetiresOldPreviewsEvenBeforeDependenciesLoad)
{
    Key.Identity.RendererRevision = 2;
    Resolution                    = ThumbnailCacheMaintenance::Resolution::SourceOnly;
    const auto old                = File("0000000000000123-0000000000000456-3-1.png");
    const auto current            = File("0000000000000123-0000000000000456-3-2.png");
    const auto before             = fs::last_write_time(current);
    const auto stats              = Cleanup(nullptr, 2);
    EXPECT_EQ(stats.Removed, 1u);
    EXPECT_FALSE(fs::exists(old));
    EXPECT_TRUE(fs::exists(current));
    EXPECT_EQ(before, fs::last_write_time(current));
}
