#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Memory;
using ZEngine::Core::Containers::ArrayView;

namespace
{
    ArrayView<const uint8_t> Bytes(const void* data, size_t size)
    {
        return ArrayView<const uint8_t>(static_cast<const uint8_t*>(data), size);
    }
} // namespace

class MetaFileIOTest : public ::testing::Test
{
protected:
    MemoryManager    m_manager;
    VFSMemoryBackend m_backend;
    VFSContext       m_ctx;

    void             SetUp() override
    {
        m_manager.Initialize(ZMega(16), {});
        m_backend.Initialize(&m_manager.MainArena);
        m_ctx.Initialize(&m_manager.MainArena, 4);
        m_ctx.Mount(&m_backend, VFSPath::Root(), 0);
    }

    void TearDown() override
    {
        m_ctx.Shutdown();
        // VFSMemoryBackend has no Shutdown(); its destructor accesses arena-backed nodes.
        // Calling m_manager.Shutdown() before ~VFSMemoryBackend causes a use-after-free on
        // Windows (SEH 0xc0000005). OS reclaims memory when the test process exits.
    }

    VFSPath P(const char* raw)
    {
        auto r = VFSPath::Parse(raw);
        EXPECT_TRUE(r.Succeeded()) << "bad path: " << raw;
        return r.Value();
    }

    void WriteRaw(const char* path, const char* content)
    {
        m_backend.WriteFile(P(path), Bytes(content, std::strlen(content)));
    }

    bool FileExists(const char* path)
    {
        auto r = m_ctx.Exists(P(path));
        return r.Succeeded() && r.Value();
    }
};

// Test 1 — MetaPathFor appends .meta suffix
TEST_F(MetaFileIOTest, MetaPathForAppendsMetaSuffix)
{
    VFSPath asset = P("/project/textures/diffuse.png");
    auto    meta  = MetaFileIO::MetaPathFor(asset);
    ASSERT_TRUE(meta.Succeeded());
    EXPECT_STREQ(meta.Value().CStr(), "/project/textures/diffuse.png.meta");
}

// Test 2 — Read returns Fail when file does not exist
TEST_F(MetaFileIOTest, ReadMissingReturnsError)
{
    auto result = MetaFileIO::Read(m_ctx, P("/ghost.glb"));
    EXPECT_TRUE(result.Failed());
}

// Test 3 — Malformed JSON returns Fail
TEST_F(MetaFileIOTest, MalformedJSONReturnsError)
{
    WriteRaw("/x.glb.meta", "{not valid json{{{{");
    auto result = MetaFileIO::Read(m_ctx, P("/x.glb"));
    EXPECT_TRUE(result.Failed());
    EXPECT_EQ(result.Error(), VFSError::Corrupted);
}

TEST_F(MetaFileIOTest, InvalidIdentitiesAreCorruptAndGetOrCreateRepairsThem)
{
    for (const auto* document : {"{}", "{\"uuid\":42}", "{\"uuid\":\"invalid\"}", "{\"uuid\":\"00000000-0000-0000-0000-000000000000\"}"})
    {
        SCOPED_TRACE(document);
        WriteRaw("/x.glb.meta", document);
        EXPECT_EQ(MetaFileIO::Read(m_ctx, P("/x.glb")).Error(), VFSError::Corrupted);
        auto repaired = MetaFileIO::GetOrCreate(m_ctx, P("/x.glb"), "Test", 0);
        ASSERT_TRUE(repaired.Succeeded());
        EXPECT_FALSE(repaired.Value().AssetUUID.is_nil());
        EXPECT_EQ(repaired.Value().Status, ImportStatus::New);
        auto persisted = MetaFileIO::Read(m_ctx, P("/x.glb"));
        ASSERT_TRUE(persisted.Succeeded());
        EXPECT_EQ(persisted.Value().AssetUUID, repaired.Value().AssetUUID);
    }
}

TEST_F(MetaFileIOTest, WritingNilIdentityIsRejected)
{
    EXPECT_EQ(MetaFileIO::Write(m_ctx, P("/x.glb"), {}).Error(), VFSError::Corrupted);
    EXPECT_FALSE(FileExists("/x.glb.meta"));
}

// Test 4 — Round-trip: Write then Read returns identical data
TEST_F(MetaFileIOTest, RoundTrip)
{
    MetaFileData in{};
    in.AssetUUID = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440000").value();
    ZEngine::Helpers::secure_strcpy(in.ImporterName, sizeof(in.ImporterName), "AssimpImporter");
    in.SourceHash       = 0xDEADBEEFCAFEBABEULL;
    in.LastImportTimeNs = 1748000000000000000LL;
    in.SettingsCount    = 1;
    ZEngine::Helpers::secure_strcpy(in.Settings[0].Key, sizeof(in.Settings[0].Key), "FlipUVs");
    ZEngine::Helpers::secure_strcpy(in.Settings[0].Value, sizeof(in.Settings[0].Value), "true");

    ASSERT_TRUE(MetaFileIO::Write(m_ctx, P("/project/mesh.glb"), in).Succeeded());

    auto out_result = MetaFileIO::Read(m_ctx, P("/project/mesh.glb"));
    ASSERT_TRUE(out_result.Succeeded());
    const MetaFileData& out = out_result.Value();

    EXPECT_EQ(in.AssetUUID, out.AssetUUID);
    EXPECT_STREQ(in.ImporterName, out.ImporterName);
    EXPECT_EQ(in.SourceHash, out.SourceHash);
    EXPECT_EQ(in.LastImportTimeNs, out.LastImportTimeNs);
    ASSERT_EQ(out.SettingsCount, 1u);
    EXPECT_STREQ(out.Settings[0].Key, "FlipUVs");
    EXPECT_STREQ(out.Settings[0].Value, "true");
}

// Test 5 — ImportStatus is never written to JSON
TEST_F(MetaFileIOTest, StatusNotSerialised)
{
    MetaFileData d{};
    d.AssetUUID = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440000").value();
    d.Status    = ImportStatus::Stale;
    ASSERT_TRUE(MetaFileIO::Write(m_ctx, P("/x.glb"), d).Succeeded());

    auto open = m_ctx.Open(MetaFileIO::MetaPathFor(P("/x.glb")).Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(open.Succeeded());
    auto*   file      = open.Value();
    uint8_t raw[4096] = {};
    file->ReadAll({raw, sizeof(raw) - 1});
    m_ctx.Close(file);

    EXPECT_EQ(std::strstr(reinterpret_cast<char*>(raw), "status"), nullptr);
    EXPECT_EQ(std::strstr(reinterpret_cast<char*>(raw), "stale"), nullptr);
}

// Test 6 — GetOrCreate on a new file returns New and creates .meta
TEST_F(MetaFileIOTest, GetOrCreateNewFile)
{
    WriteRaw("/project/mesh.glb", "binary_placeholder");

    auto result = MetaFileIO::GetOrCreate(m_ctx, P("/project/mesh.glb"), "AssimpImporter", 0xAABBCCDDULL);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Value().Status, ImportStatus::New);
    EXPECT_FALSE(result.Value().AssetUUID.is_nil());
    EXPECT_TRUE(FileExists("/project/mesh.glb.meta"));
}

// Test 7 — GetOrCreate with matching hash returns UpToDate, UUID unchanged
TEST_F(MetaFileIOTest, GetOrCreateMatchingHashReturnsUpToDate)
{
    WriteRaw("/project/mesh.glb", "binary_placeholder");

    auto first = MetaFileIO::GetOrCreate(m_ctx, P("/project/mesh.glb"), "AssimpImporter", 0x1234ULL);
    ASSERT_TRUE(first.Succeeded());
    uuids::uuid uuid_first = first.Value().AssetUUID;

    auto        second     = MetaFileIO::GetOrCreate(m_ctx, P("/project/mesh.glb"), "AssimpImporter", 0x1234ULL);
    ASSERT_TRUE(second.Succeeded());
    EXPECT_EQ(second.Value().Status, ImportStatus::UpToDate);
    EXPECT_EQ(second.Value().AssetUUID, uuid_first);
}

// Test 8 — GetOrCreate with changed hash returns Stale, UUID unchanged
TEST_F(MetaFileIOTest, GetOrCreateChangedHashReturnsStale)
{
    WriteRaw("/project/mesh.glb", "binary_placeholder");

    auto first = MetaFileIO::GetOrCreate(m_ctx, P("/project/mesh.glb"), "AssimpImporter", 0x1111ULL);
    ASSERT_TRUE(first.Succeeded());
    uuids::uuid original_uuid = first.Value().AssetUUID;

    auto        second        = MetaFileIO::GetOrCreate(m_ctx, P("/project/mesh.glb"), "AssimpImporter", 0x2222ULL);
    ASSERT_TRUE(second.Succeeded());
    EXPECT_EQ(second.Value().Status, ImportStatus::Stale);
    EXPECT_EQ(second.Value().AssetUUID, original_uuid);
}

// Generated asset data is published by rename, so watchers never see a partial file.
TEST_F(MetaFileIOTest, AtomicWritePublishesDestinationWithoutTemporaryFile)
{
    constexpr char content[] = "texture-bytes";
    ASSERT_TRUE(WriteFileAtomically(m_ctx, P("/generated/diffuse.png"), Bytes(content, sizeof(content) - 1)).Succeeded());

    EXPECT_TRUE(FileExists("/generated/diffuse.png"));
    EXPECT_FALSE(FileExists("/generated/diffuse.png.tmp"));

    auto open = m_ctx.Open(P("/generated/diffuse.png"), VFSOpenFlags::Read);
    ASSERT_TRUE(open.Succeeded());
    uint8_t bytes[sizeof(content)] = {};
    ASSERT_TRUE(open.Value()->ReadAll({bytes, sizeof(content) - 1}).Succeeded());
    m_ctx.Close(open.Value());

    EXPECT_STREQ(reinterpret_cast<const char*>(bytes), content);
}

// Test 10 — Truncated key/value strings do not overflow buffers
TEST_F(MetaFileIOTest, OverlongSettingsAreTruncated)
{
    char long_key[201];
    std::memset(long_key, 'k', 200);
    long_key[200] = '\0';

    char json[2048];
    std::snprintf(
        json,
        sizeof(json),
        "{\"uuid\":\"550e8400-e29b-41d4-a716-446655440000\","
        "\"importer\":\"Test\",\"source_hash\":0,\"import_time_ns\":0,"
        "\"artifact_path\":\"\","
        "\"settings\":[{\"key\":\"%s\",\"value\":\"v\"}]}",
        long_key);
    WriteRaw("/x.glb.meta", json);

    auto result = MetaFileIO::Read(m_ctx, P("/x.glb"));
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Value().Settings[0].Key[63], '\0');
}

// Test 11 — Settings count is capped at META_MAX_SETTINGS
TEST_F(MetaFileIOTest, SettingsCountCappedAtMax)
{
    char json[8192];
    int  pos  = 0;
    pos      += std::snprintf(
        json + pos,
        sizeof(json) - pos,
        "{\"uuid\":\"550e8400-e29b-41d4-a716-446655440000\","
        "\"importer\":\"Test\",\"source_hash\":0,\"import_time_ns\":0,"
        "\"artifact_path\":\"\",\"settings\":[");

    for (uint32_t i = 0; i < META_MAX_SETTINGS + 5; ++i)
    {
        if (i > 0)
            pos += std::snprintf(json + pos, sizeof(json) - pos, ",");
        pos += std::snprintf(json + pos, sizeof(json) - pos, "{\"key\":\"%u\",\"value\":\"v\"}", i);
    }
    pos += std::snprintf(json + pos, sizeof(json) - pos, "]}");
    WriteRaw("/x.glb.meta", json);

    auto result = MetaFileIO::Read(m_ctx, P("/x.glb"));
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Value().SettingsCount, META_MAX_SETTINGS);
}

TEST_F(MetaFileIOTest, SidecarPathBoundaryNeverOverwritesTheAsset)
{
    const std::string longest = "/" + std::string(MAX_FILE_PATH_COUNT - 2 - std::string_view(".meta").size(), 'x');
    auto              sidecar = MetaFileIO::MetaPathFor(P(longest.c_str()));
    ASSERT_TRUE(sidecar.Succeeded());
    EXPECT_EQ(sidecar.Value().Length(), MAX_FILE_PATH_COUNT - 1);
    const auto invalid = longest + "x";
    WriteRaw(invalid.c_str(), "original asset bytes");
    MetaFileData data;
    EXPECT_EQ(MetaFileIO::Write(m_ctx, P(invalid.c_str()), data).Error(), VFSError::InvalidPath);
    EXPECT_EQ(MetaFileIO::Read(m_ctx, P(invalid.c_str())).Error(), VFSError::InvalidPath);
    EXPECT_EQ(MetaFileIO::GetOrCreate(m_ctx, P(invalid.c_str()), "Test", 0).Error(), VFSError::InvalidPath);
    EXPECT_EQ(MetaFileIO::MetaPathFor(VFSPath{}).Error(), VFSError::InvalidPath);
    EXPECT_EQ(MetaFileIO::MetaPathFor(VFSPath::Root()).Error(), VFSError::InvalidPath);
    auto file = m_ctx.Open(P(invalid.c_str()), VFSOpenFlags::Read);
    ASSERT_TRUE(file.Succeeded());
    uint8_t bytes[32] = {};
    ASSERT_TRUE(file.Value()->ReadAll({bytes, sizeof(bytes) - 1}).Succeeded());
    m_ctx.Close(file.Value());
    EXPECT_STREQ(reinterpret_cast<const char*>(bytes), "original asset bytes");
}
