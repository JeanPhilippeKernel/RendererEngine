#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;

namespace
{
    struct FaultFile : IVFSFile
    {
        IVFSFile*         File        = nullptr;
        uint64_t          ReadLimit   = UINT64_MAX;
        size_t            MaxChunk    = SIZE_MAX;
        bool              ReadIOError = false;
        bool              SizeIOError = false;
        VFSResult<size_t> Read(ZEngine::Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset) override
        {
            if (offset >= ReadLimit)
                return ReadIOError ? VFSResult<size_t>::Fail(VFSError::IOError) : VFSResult<size_t>::Ok(0);
            const size_t count = std::min({buffer.size(), MaxChunk, static_cast<size_t>(ReadLimit - offset)});
            return File->Read({buffer.data(), count}, offset);
        }
        VFSResult<size_t> Write(ZEngine::Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset) override
        {
            return File->Write(buffer, offset);
        }
        VFSResult<uint64_t> Size() const override
        {
            return SizeIOError ? VFSResult<uint64_t>::Fail(VFSError::IOError) : File->Size();
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

    // Faults apply to scanner operations; direct backend access remains available
    // to verify that failed scans did not alter the original sidecar.
    struct FaultContext : VFSContext
    {
        bool                 DenyMetaOnce  = false;
        bool                 FailAssetRead = false;
        bool                 FailWrite     = false;
        bool                 FailRename    = false;
        bool                 FailList      = false;
        int                  Writes        = 0;
        FaultFile            AssetFile;
        bool                 WrapAssetReads    = false;
        bool                 WrapMetadataReads = false;
        VFSResult<IVFSFile*> Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            if (HasFlag(flags, VFSOpenFlags::Read))
            {
                if (path.Extension().Equals(".meta") && DenyMetaOnce)
                {
                    DenyMetaOnce = false;
                    return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
                }
                if (!path.Extension().Equals(".meta") && FailAssetRead)
                    return VFSResult<IVFSFile*>::Fail(VFSError::IOError);
            }
            if (HasFlag(flags, VFSOpenFlags::Write))
            {
                ++Writes;
                if (FailWrite)
                    return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
            }
            auto result = VFSContext::Open(path, flags);
            if (result.Succeeded() && HasFlag(flags, VFSOpenFlags::Read) && ((WrapAssetReads && !path.Extension().Equals(".meta")) || (WrapMetadataReads && path.Extension().Equals(".meta"))))
            {
                AssetFile.File = result.Value();
                return VFSResult<IVFSFile*>::Ok(&AssetFile);
            }
            return result;
        }
        void Close(IVFSFile* file) override
        {
            VFSContext::Close(file == &AssetFile ? AssetFile.File : file);
        }
        VFSResult<void> Rename(const VFSPath& from, const VFSPath& to) override
        {
            return FailRename ? VFSResult<void>::Fail(VFSError::IOError) : VFSContext::Rename(from, to);
        }
        VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>> List(const VFSPath& path, ArenaAllocator* arena) override
        {
            if (FailList)
                return VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>>::Fail(VFSError::IOError);
            return VFSContext::List(path, arena);
        }
    };
} // namespace

class VFSAssetIdentityTest : public ::testing::Test
{
protected:
    MemoryManager     Memory;
    VFSMemoryBackend  Backend;
    FaultContext      Context;
    const uuids::uuid ID = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440000").value();
    void              SetUp() override
    {
        Memory.Initialize(ZGiga(1), {});
        Backend.Initialize(&Memory.MainArena);
        Context.Initialize(&Memory.MainArena, 4);
        ASSERT_TRUE(Context.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
    }
    void TearDown() override
    {
        Context.Shutdown();
    }
    VFSPath P(const char* path)
    {
        return VFSPath::Parse(path).Value();
    }
    void Put(const char* path, const std::string& data)
    {
        ASSERT_TRUE(Backend.WriteFile(P(path), {reinterpret_cast<const uint8_t*>(data.data()), data.size()}).Succeeded());
    }
    std::string Material()
    {
        return "{\"uuid\":\"" + uuids::to_string(ID) + "\",\"textures\":{\"albedo\":{\"path\":\"" + std::string(200, 'p') + "\"}}}";
    }
    void Scan(VFSScanner& scanner, VFSDirectoryCache& cache, ScanStats& stats)
    {
        std::atomic<bool> complete{false};
        struct Completion
        {
            ScanStats*         Stats;
            std::atomic<bool>* Complete;
        } completion{&stats, &complete};
        scanner.SetOnScanComplete(&completion, [](void* ctx, ScanStats value) {
            auto* done   = static_cast<Completion*>(ctx);
            *done->Stats = value;
            done->Complete->store(true);
        });
        scanner.Scan(&Context, VFSPath::Root(), &cache);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!complete.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // Keep callback context alive until all tasks finish, even on a timeout.
        if (!complete.load())
        {
            scanner.Cancel();
            while (scanner.IsScanning())
                std::this_thread::yield();
        }
        ASSERT_TRUE(complete.load());
    }
};

TEST_F(VFSAssetIdentityTest, Reads1000MaterialIdentitiesWithoutAnArena)
{
    Put("/material.zematerial", Material());
    for (int i = 0; i < 1000; ++i)
    {
        auto result = ReadEmbeddedAssetUUID(Context, P("/material.zematerial"));
        ASSERT_TRUE(result.Succeeded());
        EXPECT_EQ(result.Value(), ID);
    }
}

TEST_F(VFSAssetIdentityTest, StreamsLargeMaterialAndUsesOnlyTopLevelIdentity)
{
    const std::string json = "{\"textures\":{\"albedo\":{\"uuid\":\"550e8400-e29b-41d4-a716-446655440001\",\"path\":\"" + std::string(20000, 'p') + "\"}},\"uuid\":\"" + uuids::to_string(ID) + "\"}";
    Put("/large.zematerial", json);
    auto result = ReadEmbeddedAssetUUID(Context, P("/large.zematerial"));
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Value(), ID);
}

TEST_F(VFSAssetIdentityTest, OversizedMaterialIdentityIsRejectedWithoutReplacingCorruptMetadata)
{
    Put("/large.zematerial", "{\"uuid\":\"" + uuids::to_string(ID) + "\",\"name\":\"" + std::string(MATERIAL_IDENTITY_MAX_FILE_SIZE, 'n') + "\"}");
    Put("/large.zematerial.meta", "{corrupt metadata");

    EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/large.zematerial")).Error(), VFSError::SizeLimitExceeded);

    VFSDirectoryCache cache;
    cache.Initialize(&Memory.MainArena);
    VFSScanner scanner;
    scanner.Initialize(&Memory.MainArena);
    Context.Writes = 0;
    ScanStats stats;
    Scan(scanner, cache, stats);

    EXPECT_EQ(stats.Errors, 1u);
    EXPECT_EQ(Context.Writes, 0);
    auto sidecar = Context.Open(P("/large.zematerial.meta"), VFSOpenFlags::Read);
    ASSERT_TRUE(sidecar.Succeeded());
    std::array<uint8_t, 18> contents{};
    auto                    read = sidecar.Value()->ReadAll({contents.data(), contents.size()});
    Context.Close(sidecar.Value());
    ASSERT_TRUE(read.Succeeded());
    EXPECT_STREQ(reinterpret_cast<const char*>(contents.data()), "{corrupt metadata");
}

TEST_F(VFSAssetIdentityTest, RejectsInvalidMaterialsAndTruncatedMeshes)
{
    for (const char* json : {"{}", "[]", "{\"uuid\":null}", "{\"uuid\":\"invalid\"}", "{\"uuid\":\"00000000-0000-0000-0000-000000000000\"}", "{\"nested\":{\"uuid\":\"550e8400-e29b-41d4-a716-446655440000\"}}", "{\"uuid\":\"550e8400-e29b-41d4-a716-446655440000\"}garbage"})
    {
        Put("/bad.zematerial", json);
        EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/bad.zematerial")).Error(), VFSError::Corrupted);
    }
    Put("/bad.zemesh", std::string(23, '\0'));
    EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/bad.zemesh")).Error(), VFSError::Corrupted);
    Context.FailAssetRead = true;
    EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/bad.zemesh")).Error(), VFSError::IOError);
}

TEST_F(VFSAssetIdentityTest, ScannerPreservesSidecarOnMetadataHashAndPublicationFailures)
{
    Put("/material.zematerial", Material());
    MetaFileData data{};
    data.AssetUUID     = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440001").value();
    data.SourceHash    = 123;
    data.SettingsCount = 1;
    std::strcpy(data.Settings[0].Key, "keep");
    std::strcpy(data.Settings[0].Value, "settings");
    ASSERT_TRUE(MetaFileIO::Write(Context, P("/material.zematerial"), data).Succeeded());
    VFSDirectoryCache cache;
    cache.Initialize(&Memory.MainArena);
    VFSScanner scanner;
    scanner.Initialize(&Memory.MainArena);
    for (int fault = 0; fault < 4; ++fault)
    {
        SCOPED_TRACE(fault);
        Context.DenyMetaOnce  = fault == 0;
        Context.FailAssetRead = fault == 1;
        Context.FailWrite     = fault == 2;
        Context.FailRename    = fault == 3;
        Context.Writes        = 0;
        ScanStats stats;
        Scan(scanner, cache, stats);
        EXPECT_EQ(stats.Errors, 1u);
        EXPECT_EQ(stats.MetasCreated + stats.MetasUpdated + stats.MetasUpToDate, 0u);
        if (fault < 2)
            EXPECT_EQ(Context.Writes, 0);
        Context.FailAssetRead = Context.FailWrite = Context.FailRename = false;
        auto after                                                     = MetaFileIO::Read(Context, P("/material.zematerial"));
        ASSERT_TRUE(after.Succeeded());
        EXPECT_EQ(after.Value().AssetUUID, data.AssetUUID);
        EXPECT_EQ(after.Value().SourceHash, data.SourceHash);
        EXPECT_EQ(after.Value().SettingsCount, 1u);
        EXPECT_STREQ(after.Value().Settings[0].Value, "settings");
    }
}

TEST_F(VFSAssetIdentityTest, ScannerReportsDirectoryErrorsAndDoesNotCreateIdentityForInvalidCookedAsset)
{
    VFSDirectoryCache cache;
    cache.Initialize(&Memory.MainArena);
    VFSScanner scanner;
    scanner.Initialize(&Memory.MainArena);
    Context.FailList = true;
    ScanStats stats;
    Scan(scanner, cache, stats);
    EXPECT_EQ(stats.Errors, 1u);
    Context.FailList = false;
    Put("/bad.zematerial", "{}");
    Scan(scanner, cache, stats);
    EXPECT_EQ(stats.Errors, 1u);
    EXPECT_FALSE(Context.Exists(P("/bad.zematerial.meta")).Value());
}

TEST_F(VFSAssetIdentityTest, ScannerSeedsLargeMaterialIdentityAndPreservesOversizedSidecar)
{
    Put("/large.zematerial", "{\"uuid\":\"" + uuids::to_string(ID) + "\",\"name\":\"" + std::string(20000, 'n') + "\"}");
    VFSDirectoryCache cache;
    cache.Initialize(&Memory.MainArena);
    VFSScanner scanner;
    scanner.Initialize(&Memory.MainArena);
    ScanStats stats;
    Scan(scanner, cache, stats);
    EXPECT_EQ(stats.Errors, 0u);
    EXPECT_EQ(stats.MetasCreated, 1u);
    auto meta = MetaFileIO::Read(Context, P("/large.zematerial"));
    ASSERT_TRUE(meta.Succeeded());
    EXPECT_EQ(meta.Value().AssetUUID, ID);
    Put("/large.zematerial.meta", std::string(MetaFileIO::MaxFileSize + 1, ' '));
    Context.Writes = 0;
    Scan(scanner, cache, stats);
    EXPECT_EQ(stats.Errors, 1u);
    EXPECT_EQ(Context.Writes, 0);
    EXPECT_EQ(MetaFileIO::Read(Context, P("/large.zematerial")).Error(), VFSError::SizeLimitExceeded);
}

TEST_F(VFSAssetIdentityTest, IdentityReadsPropagateIOErrorsAndDetectPrematureEOF)
{
    Put("/material.zematerial", "{\"uuid\":\"" + uuids::to_string(ID) + "\",\"name\":\"" + std::string(5000, 'n') + "\"}");
    Context.WrapAssetReads      = true;
    Context.AssetFile.ReadLimit = 4096;
    EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/material.zematerial")).Error(), VFSError::IOError);
    Context.AssetFile.ReadIOError = true;
    EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/material.zematerial")).Error(), VFSError::IOError);
    Context.AssetFile.ReadLimit   = UINT64_MAX;
    Context.AssetFile.SizeIOError = true;
    EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/material.zematerial")).Error(), VFSError::IOError);
    Context.AssetFile.SizeIOError = false;
    Context.AssetFile.MaxChunk    = 7;
    auto material                 = ReadEmbeddedAssetUUID(Context, P("/material.zematerial"));
    ASSERT_TRUE(material.Succeeded());
    EXPECT_EQ(material.Value(), ID);
    std::string mesh(24, '\0');
    uint32_t    magic = ZEMESH_MAGIC, version = ASSET_FILE_VERSION;
    std::memcpy(mesh.data(), &magic, 4);
    std::memcpy(mesh.data() + 4, &version, 4);
    std::memcpy(mesh.data() + 8, &ID, 16);
    Put("/mesh.zemesh", mesh);
    auto header = ReadEmbeddedAssetUUID(Context, P("/mesh.zemesh"));
    ASSERT_TRUE(header.Succeeded());
    EXPECT_EQ(header.Value(), ID);
    Context.AssetFile.ReadLimit   = 12;
    Context.AssetFile.ReadIOError = false;
    EXPECT_EQ(ReadEmbeddedAssetUUID(Context, P("/mesh.zemesh")).Error(), VFSError::Corrupted);
}

TEST_F(VFSAssetIdentityTest, ScannerDoesNotPersistHashFromPrematureEOFOrFailedSizeQuery)
{
    Put("/material.zematerial", Material());
    MetaFileData before{};
    before.AssetUUID  = ID;
    before.SourceHash = 123;
    ASSERT_TRUE(MetaFileIO::Write(Context, P("/material.zematerial"), before).Succeeded());
    Context.Writes         = 0;
    Context.WrapAssetReads = true;
    VFSDirectoryCache cache;
    cache.Initialize(&Memory.MainArena);
    VFSScanner scanner;
    scanner.Initialize(&Memory.MainArena);
    for (bool size_failure : {false, true})
    {
        Context.AssetFile.ReadLimit   = size_failure ? UINT64_MAX : 12;
        Context.AssetFile.SizeIOError = size_failure;
        ScanStats stats;
        Scan(scanner, cache, stats);
        EXPECT_EQ(stats.Errors, 1u);
        EXPECT_EQ(Context.Writes, 0);
        auto retained = MetaFileIO::Read(Context, P("/material.zematerial"));
        ASSERT_TRUE(retained.Succeeded());
        EXPECT_EQ(retained.Value().SourceHash, before.SourceHash);
        EXPECT_EQ(retained.Value().AssetUUID, before.AssetUUID);
    }
}

TEST_F(VFSAssetIdentityTest, ShortMetadataReadsAreIOErrorsAndNeverTriggerRepair)
{
    MetaFileData before{};
    before.AssetUUID  = ID;
    before.SourceHash = 123;
    ASSERT_TRUE(MetaFileIO::Write(Context, P("/material.zematerial"), before).Succeeded());
    Context.Writes              = 0;
    Context.WrapMetadataReads   = true;
    Context.AssetFile.ReadLimit = 32;
    EXPECT_EQ(MetaFileIO::Read(Context, P("/material.zematerial")).Error(), VFSError::IOError);
    EXPECT_EQ(MetaFileIO::GetOrCreate(Context, P("/material.zematerial"), "Test", 456, true).Error(), VFSError::IOError);
    EXPECT_EQ(Context.Writes, 0);
    Context.WrapMetadataReads = false;
    auto retained             = MetaFileIO::Read(Context, P("/material.zematerial"));
    ASSERT_TRUE(retained.Succeeded());
    EXPECT_EQ(retained.Value().AssetUUID, before.AssetUUID);
    EXPECT_EQ(retained.Value().SourceHash, before.SourceHash);
}

TEST_F(VFSAssetIdentityTest, HashIsStableAcrossPartialBackendReads)
{
    Put("/material.zematerial", "{\"uuid\":\"" + uuids::to_string(ID) + "\",\"name\":\"" + std::string(5000, 'n') + "\"}");
    auto expected = MetaFileIO::ComputeHash(Context, P("/material.zematerial"));
    ASSERT_TRUE(expected.Succeeded());
    Context.WrapAssetReads     = true;
    Context.AssetFile.MaxChunk = 7;
    auto fragmented            = MetaFileIO::ComputeHash(Context, P("/material.zematerial"));
    ASSERT_TRUE(fragmented.Succeeded());
    EXPECT_EQ(fragmented.Value(), expected.Value());
}
