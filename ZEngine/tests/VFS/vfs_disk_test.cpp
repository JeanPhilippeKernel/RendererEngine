#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>
#include "../TemporaryDirectory.h"

using namespace ZEngine::Core::VFS;
using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Containers::String;
using ZEngine::Core::Memory::MemoryManager;
using ZEngine::Helpers::secure_memcmp;
using ZEngine::Helpers::secure_memset;
using ZEngine::Helpers::secure_strlen;

namespace
{
    bool BytesEqual(const uint8_t* data, size_t len, cstring expected)
    {
        const size_t elen = secure_strlen(expected);
        return len == elen && secure_memcmp(data, len, expected, elen, len) == 0;
    }

    struct ScopedDiskFile
    {
        VFSDiskBackend* Backend = nullptr;
        IVFSFile*       File    = nullptr;
        ~ScopedDiskFile()
        {
            if (File)
                Backend->Close(File);
        }
    };
} // namespace

class VFSDiskBackendTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_manager.Initialize(ZKilo(256), {});

        std::error_code error = {};
        ASSERT_TRUE(m_directory.Create("zengine_vfs_disk_tests", error));
        ASSERT_FALSE(error);
        const auto utf8_root = m_directory.Path().generic_u8string();
        m_root.init(&m_manager.MainArena, reinterpret_cast<const char*>(utf8_root.c_str()));

        m_backend.Initialize(m_root.c_str(), VFSBackendCaps::Read | VFSBackendCaps::Write, &m_manager.MainArena);
    }

    void TearDown() override
    {
        m_manager.Shutdown();
    }

    void WriteFile(cstring rel_path, cstring data)
    {
        const size_t         n      = secure_strlen(data);
        VFSResult<IVFSFile*> opened = m_backend.Open(VFSPath::Parse(rel_path).Value(), VFSOpenFlags::Write);
        ASSERT_TRUE(opened.Succeeded());

        IVFSFile*         file  = opened.Value();
        VFSResult<size_t> wrote = file->Write(ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(data), n), 0);
        EXPECT_TRUE(wrote.Succeeded());
        EXPECT_EQ(wrote.Value(), n);
        m_backend.Close(file);
    }

    Array<uint8_t> ReadBuffer(size_t n)
    {
        Array<uint8_t> buffer;
        buffer.init(&m_manager.MainArena, n, n);
        return buffer;
    }

    String                             m_root = {};
    MemoryManager                      m_manager{};
    VFSDiskBackend                     m_backend   = {};
    ZEngine::Tests::TemporaryDirectory m_directory = {};
};

TEST_F(VFSDiskBackendTest, OpenMissingFileFailsWithNotFound)
{
    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/nope.txt").Value(), VFSOpenFlags::Read);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::NotFound);
}

TEST_F(VFSDiskBackendTest, InvalidPathsCannotAliasTheMountedRootOrConsumeFileSlots)
{
    VFSDiskBackend backend = {};
    backend.Initialize(m_root.c_str(), VFSBackendCaps::Read | VFSBackendCaps::Write, &m_manager.MainArena, 1);
    WriteFile("/sentinel.zescene", "retained");
    const VFSPath invalid = {};
    ASSERT_FALSE(invalid.IsValid());
    for (uint32_t attempt = 0; attempt < 8; ++attempt)
    {
        auto           opened = backend.Open(invalid, VFSOpenFlags::Read);
        // Own even an unexpectedly successful old implementation's descriptor.
        ScopedDiskFile file   = {&backend, opened.Succeeded() ? opened.Value() : nullptr};
        ASSERT_TRUE(opened.Failed());
        EXPECT_EQ(opened.Error(), VFSError::PermissionDenied);
    }
    EXPECT_FALSE(backend.Exists(invalid));
    EXPECT_EQ(backend.Stat(invalid).Error(), VFSError::InvalidPath);
    EXPECT_EQ(backend.List(&m_manager.MainArena, invalid).Error(), VFSError::InvalidPath);
    EXPECT_TRUE(backend.CreateDir(invalid).Failed());
    EXPECT_TRUE(backend.Remove(invalid).Failed());
    EXPECT_TRUE(backend.RemoveAll(invalid).Failed());
    EXPECT_TRUE(m_backend.Exists(VFSPath::Parse("/sentinel.zescene").Value()));
    auto           opened = backend.Open(VFSPath::Parse("/sentinel.zescene").Value(), VFSOpenFlags::Read);
    ScopedDiskFile file   = {&backend, opened.Succeeded() ? opened.Value() : nullptr};
    ASSERT_TRUE(opened.Succeeded());
    auto buffer = ReadBuffer(8);
    auto read   = file.File->ReadAll({buffer.data(), buffer.size()});
    ASSERT_TRUE(read.Succeeded());
    EXPECT_TRUE(BytesEqual(buffer.data(), read.Value(), "retained"));
}

TEST_F(VFSDiskBackendTest, Utf8RootAndNamesUseTheActualNativeFilesForEveryDiskOperation)
{
    const auto      native_root = m_directory.Path() / std::filesystem::path(u8"scène_東京_🚀");
    std::error_code error       = {};
    ASSERT_TRUE(std::filesystem::create_directory(native_root, error));
    ASSERT_FALSE(error);
    const auto     utf8_root = native_root.generic_u8string();
    VFSDiskBackend backend   = {};
    backend.Initialize(reinterpret_cast<const char*>(utf8_root.c_str()), VFSBackendCaps::Read | VFSBackendCaps::Write, &m_manager.MainArena, 4);
    const auto directory        = VFSPath::Parse(reinterpret_cast<const char*>(u8"/資料_🚀")).Value();
    const auto original         = directory.Append(reinterpret_cast<const char*>(u8"café.zescene")).Value();
    const auto moved            = directory.Append(reinterpret_cast<const char*>(u8"résultat_東京.zescene")).Value();
    const auto native_directory = native_root / std::filesystem::path(u8"資料_🚀");
    const auto native_original  = native_directory / std::filesystem::path(u8"café.zescene");
    const auto native_moved     = native_directory / std::filesystem::path(u8"résultat_東京.zescene");
    ASSERT_TRUE(backend.CreateDirExclusive(directory).Succeeded());
    EXPECT_EQ(backend.CreateDirExclusive(directory).Error(), VFSError::AlreadyExists);
    EXPECT_TRUE(backend.CreateDir(directory).Succeeded());
    ASSERT_TRUE(std::filesystem::is_directory(native_directory, error));
    ASSERT_FALSE(error);
    {
        auto           opened = backend.Open(original, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive);
        ScopedDiskFile file   = {&backend, opened.Succeeded() ? opened.Value() : nullptr};
        ASSERT_TRUE(opened.Succeeded());
        constexpr uint8_t contents[] = {'s', 'c', 'e', 'n', 'e'};
        const auto        written    = file.File->Write({contents, sizeof(contents)}, 0);
        ASSERT_TRUE(written.Succeeded());
        ASSERT_EQ(written.Value(), sizeof(contents));
        ASSERT_TRUE(file.File->Flush().Succeeded());
    }
    // Check with independently constructed native paths, not another backend
    // lookup that could repeat the same accidental ANSI/locale conversion.
    ASSERT_TRUE(std::filesystem::exists(native_original, error));
    ASSERT_FALSE(error);
    EXPECT_EQ(std::filesystem::file_size(native_original, error), 5u);
    ASSERT_FALSE(error);
    EXPECT_TRUE(backend.Exists(original));
    auto stat = backend.Stat(original);
    ASSERT_TRUE(stat.Succeeded());
    EXPECT_FALSE(stat.Value().IsDirectory);
    EXPECT_EQ(stat.Value().SizeBytes, 5u);
    {
        auto           opened = backend.Open(original, VFSOpenFlags::Read);
        ScopedDiskFile file   = {&backend, opened.Succeeded() ? opened.Value() : nullptr};
        ASSERT_TRUE(opened.Succeeded());
        auto       buffer = ReadBuffer(5);
        const auto read   = file.File->ReadAll({buffer.data(), buffer.size()});
        ASSERT_TRUE(read.Succeeded());
        EXPECT_TRUE(BytesEqual(buffer.data(), read.Value(), "scene"));
        uint64_t mapped_size = 0;
        auto     mapped      = static_cast<VFSDiskFile*>(file.File)->MemoryMap(mapped_size);
        ASSERT_TRUE(mapped.Succeeded());
        EXPECT_EQ(mapped_size, 5u);
        EXPECT_TRUE(BytesEqual(static_cast<const uint8_t*>(mapped.Value()), mapped_size, "scene"));
    }
    auto listed = backend.List(&m_manager.MainArena, directory);
    ASSERT_TRUE(listed.Succeeded());
    ASSERT_EQ(listed.Value().size(), 1u);
    EXPECT_EQ(listed.Value()[0].Path, original);
    EXPECT_EQ(listed.Value()[0].Stat.SizeBytes, 5u);
    ASSERT_TRUE(backend.RenameNoReplace(original, moved).Succeeded());
    EXPECT_FALSE(std::filesystem::exists(native_original, error));
    ASSERT_FALSE(error);
    EXPECT_TRUE(std::filesystem::exists(native_moved, error));
    ASSERT_FALSE(error);
    ASSERT_TRUE(backend.Rename(moved, original).Succeeded());
    EXPECT_TRUE(std::filesystem::exists(native_original, error));
    ASSERT_FALSE(error);
    ASSERT_TRUE(backend.Remove(original).Succeeded());
    EXPECT_FALSE(std::filesystem::exists(native_original, error));
    ASSERT_FALSE(error);
    ASSERT_TRUE(backend.RemoveAll(directory).Succeeded());
    EXPECT_FALSE(std::filesystem::exists(native_directory, error));
    EXPECT_FALSE(error);
}

#if !defined(_WIN32)
TEST_F(VFSDiskBackendTest, ListingAnUnreadableDirectoryFailsInsteadOfPublishingAnEmptySnapshot)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "The root user bypasses directory read permissions";
    const auto path = VFSPath::Parse("/unreadable").Value();
    ASSERT_TRUE(m_backend.CreateDir(path).Succeeded());
    struct PermissionGuard
    {
        std::filesystem::path Path = {};
        ~PermissionGuard()
        {
            std::error_code error = {};
            std::filesystem::permissions(Path, std::filesystem::perms::owner_all, error);
        }
    } guard               = {m_directory.Path() / "unreadable"};
    std::error_code error = {};
    std::filesystem::permissions(guard.Path, std::filesystem::perms::none, error);
    ASSERT_FALSE(error);
    const size_t arena_before = m_manager.MainArena.m_current_offset;
    auto         entries      = m_backend.List(&m_manager.MainArena, path);
    ASSERT_TRUE(entries.Failed());
    EXPECT_EQ(entries.Error(), VFSError::IOError);
    EXPECT_EQ(m_manager.MainArena.m_current_offset, arena_before);
}
#endif

#if defined(_WIN32)
TEST_F(VFSDiskBackendTest, InvalidUtf8PathsNeverCreateAnsiAliasesOrConsumeFileSlots)
{
    VFSDiskBackend backend = {};
    backend.Initialize(m_root.c_str(), VFSBackendCaps::Read | VFSBackendCaps::Write, &m_manager.MainArena, 1);
    const char* const malformed[] = {"/\xc0\xaf.zescene", "/\xed\xa0\x80.zescene", "/\xf4\x90\x80\x80.zescene", "/\xe2\x82", "/\x80.zescene"};
    for (const auto* text : malformed)
    {
        const auto parsed = VFSPath::Parse(text);
        ASSERT_TRUE(parsed.Succeeded());
        auto           opened = backend.Open(parsed.Value(), VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive);
        ScopedDiskFile file   = {&backend, opened.Succeeded() ? opened.Value() : nullptr};
        ASSERT_TRUE(opened.Failed());
        EXPECT_EQ(opened.Error(), VFSError::PermissionDenied);
        EXPECT_FALSE(backend.Exists(parsed.Value()));
        EXPECT_EQ(backend.Stat(parsed.Value()).Error(), VFSError::InvalidPath);
    }
    auto listed = backend.List(&m_manager.MainArena, VFSPath::Root());
    ASSERT_TRUE(listed.Succeeded());
    EXPECT_TRUE(listed.Value().empty());
    auto           opened = backend.Open(VFSPath::Parse("/valid.zescene").Value(), VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive);
    ScopedDiskFile file   = {&backend, opened.Succeeded() ? opened.Value() : nullptr};
    EXPECT_TRUE(opened.Succeeded());
}
#endif

TEST_F(VFSDiskBackendTest, DirectoryOpenFailureIsNotReportedAsMissing)
{
    ASSERT_TRUE(std::filesystem::create_directory(m_directory.Path() / "existing"));
    auto opened = m_backend.Open(VFSPath::Parse("/existing").Value(), VFSOpenFlags::Write);
    ASSERT_TRUE(opened.Failed());
    EXPECT_NE(opened.Error(), VFSError::NotFound);
}

#if !defined(_WIN32)
TEST_F(VFSDiskBackendTest, UnreadableExistingFileReturnsPermissionDenied)
{
    WriteFile("/restricted.meta", "existing metadata");
    const auto      path = m_directory.Path() / "restricted.meta";
    std::error_code error;
    std::filesystem::permissions(path, std::filesystem::perms::none, error);
    ASSERT_FALSE(error);
    auto opened = m_backend.Open(VFSPath::Parse("/restricted.meta").Value(), VFSOpenFlags::Read);
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, error);
    ASSERT_FALSE(error);
    if (opened.Succeeded())
    {
        m_backend.Close(opened.Value());
        GTEST_SKIP() << "This user can bypass filesystem permission checks";
    }
    EXPECT_EQ(opened.Error(), VFSError::PermissionDenied);
}
#endif

TEST_F(VFSDiskBackendTest, WriteThenReadBack)
{
    cstring contents = "hello vfs world";
    WriteFile("/greeting.txt", contents);

    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/greeting.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file   = r.Value();

    Array<uint8_t>    buffer = ReadBuffer(secure_strlen(contents));
    VFSResult<size_t> read   = file->Read(ArrayView<uint8_t>(buffer.data(), buffer.size()), 0);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), secure_strlen(contents));
    EXPECT_TRUE(BytesEqual(buffer.data(), read.Value(), contents));

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, ReadAtOffset)
{
    WriteFile("/data.bin", "0123456789");

    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/data.bin").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file      = r.Value();

    uint8_t           buffer[4] = {};
    VFSResult<size_t> read      = file->Read(ArrayView<uint8_t>(buffer, 4), 3);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 4u);
    EXPECT_TRUE(BytesEqual(buffer, read.Value(), "3456"));

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, ReadPastEofReturnsZero)
{
    WriteFile("/eof.txt", "abc");

    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/eof.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file      = r.Value();

    uint8_t           buffer[4] = {};
    VFSResult<size_t> read      = file->Read(ArrayView<uint8_t>(buffer, 4), 100);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 0u);

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, Size)
{
    WriteFile("/s.txt", "12345");

    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/s.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*           file = r.Value();

    VFSResult<uint64_t> size = file->Size();
    ASSERT_TRUE(size.Succeeded());
    EXPECT_EQ(size.Value(), 5u);

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, StatReportsSize)
{
    WriteFile("/s.txt", "12345");

    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/s.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*              file = r.Value();

    VFSResult<VFSFileStat> stat = file->Stat();
    ASSERT_TRUE(stat.Succeeded());
    EXPECT_EQ(stat.Value().SizeBytes, 5u);
    EXPECT_FALSE(stat.Value().IsDirectory);

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, ReadAllStopsAtEofWithLargerBuffer)
{
    WriteFile("/small.txt", "abc");

    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/small.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*      file   = r.Value();

    Array<uint8_t> buffer = ReadBuffer(100);
    secure_memset(buffer.data(), 0xFF, buffer.size(), buffer.size());

    VFSResult<size_t> read = file->ReadAll(ArrayView<uint8_t>(buffer.data(), buffer.size()));
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 3u);
    EXPECT_TRUE(BytesEqual(buffer.data(), read.Value(), "abc"));

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, PathReturnsOpenedPath)
{
    WriteFile("/x.txt", "y");

    VFSPath              path = VFSPath::Parse("/x.txt").Value();
    VFSResult<IVFSFile*> r    = m_backend.Open(path, VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile* file = r.Value();

    EXPECT_TRUE(file->Path() == path);

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, FlushSucceeds)
{
    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/f.txt").Value(), VFSOpenFlags::Write);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file      = r.Value();

    const char        payload[] = "data";
    VFSResult<size_t> wrote     = file->Write(ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(payload), 4), 0);
    ASSERT_TRUE(wrote.Succeeded());
    EXPECT_TRUE(file->Flush().Succeeded());

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, MemoryMapReadsContents)
{
    WriteFile("/m.txt", "mapped!");

    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/m.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*        file   = r.Value();

    uint64_t         sz     = 0;
    VFSResult<void*> mapped = static_cast<VFSDiskFile*>(file)->MemoryMap(sz);
    ASSERT_TRUE(mapped.Succeeded());
    EXPECT_EQ(sz, 7u);
    EXPECT_TRUE(BytesEqual(reinterpret_cast<const uint8_t*>(mapped.Value()), 7, "mapped!"));

    m_backend.Close(file);
}

TEST_F(VFSDiskBackendTest, Exists)
{
    VFSPath path = VFSPath::Parse("/e.txt").Value();
    EXPECT_FALSE(m_backend.Exists(path));
    WriteFile("/e.txt", "hi");
    EXPECT_TRUE(m_backend.Exists(path));
}

TEST_F(VFSDiskBackendTest, ListReturnsEntries)
{
    WriteFile("/a.txt", "a");
    WriteFile("/b.txt", "bb");

    VFSResult<Array<VFSDirEntry>> result = m_backend.List(&m_manager.MainArena, VFSPath::Root());
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Value().size(), 2u);

    bool found_a = false;
    bool found_b = false;
    for (const VFSDirEntry& entry : result.Value())
    {
        found_a = found_a || entry.Path.Filename().Equals("a.txt");
        found_b = found_b || entry.Path.Filename().Equals("b.txt");
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST_F(VFSDiskBackendTest, CreateDirAndListIt)
{
    ASSERT_TRUE(m_backend.CreateDir(VFSPath::Parse("/sub").Value()).Succeeded());

    VFSResult<VFSFileStat> stat = m_backend.Stat(VFSPath::Parse("/sub").Value());
    ASSERT_TRUE(stat.Succeeded());
    EXPECT_TRUE(stat.Value().IsDirectory);
}

TEST_F(VFSDiskBackendTest, ExclusiveDirectoryCreationRejectsExistingNamesAndRequiresAnExistingParent)
{
    const auto path = VFSPath::Parse("/exclusive").Value();
    ASSERT_TRUE(m_backend.CreateDirExclusive(path).Succeeded());
    EXPECT_EQ(m_backend.CreateDirExclusive(path).Error(), VFSError::AlreadyExists);
    // Recursive CreateDir retains its idempotent importer/cache contract.
    EXPECT_TRUE(m_backend.CreateDir(path).Succeeded());
    WriteFile("/file.txt", "keep");
    EXPECT_EQ(m_backend.CreateDirExclusive(VFSPath::Parse("/file.txt").Value()).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.CreateDirExclusive(VFSPath::Parse("/absent/child").Value()).Error(), VFSError::NotFound);
    EXPECT_FALSE(m_backend.Exists(VFSPath::Parse("/absent").Value()));
    EXPECT_EQ(m_backend.CreateDirExclusive(VFSPath{}).Error(), VFSError::InvalidPath);
    EXPECT_EQ(m_backend.CreateDirExclusive(VFSPath::Root()).Error(), VFSError::AlreadyExists);
}

TEST_F(VFSDiskBackendTest, ConcurrentExclusiveDirectoryCreationHasOneWinner)
{
    constexpr int     workers          = 8;
    std::thread       threads[workers] = {};
    std::atomic<int>  ready{0}, created{0}, conflicts{0}, failures{0};
    std::atomic<bool> start{false};
    const auto        path        = VFSPath::Parse("/concurrent-directory").Value();
    const auto        arena_bytes = m_manager.MainArena.m_current_offset;
    for (auto& thread : threads)
        thread = std::thread([&]() {
            ++ready;
            while (!start.load())
                std::this_thread::yield();
            const auto result = m_backend.CreateDirExclusive(path);
            if (result.Succeeded())
                ++created;
            else if (result.Error() == VFSError::AlreadyExists)
                ++conflicts;
            else
                ++failures;
        });
    while (ready.load() != workers)
        std::this_thread::yield();
    start = true;
    for (auto& thread : threads)
        thread.join();
    EXPECT_EQ(created.load(), 1);
    EXPECT_EQ(conflicts.load(), workers - 1);
    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(m_manager.MainArena.m_current_offset, arena_bytes);
}

TEST_F(VFSDiskBackendTest, RemoveDeletesFile)
{
    WriteFile("/gone.txt", "bye");
    ASSERT_TRUE(m_backend.Exists(VFSPath::Parse("/gone.txt").Value()));

    ASSERT_TRUE(m_backend.Remove(VFSPath::Parse("/gone.txt").Value()).Succeeded());
    EXPECT_FALSE(m_backend.Exists(VFSPath::Parse("/gone.txt").Value()));
}

TEST_F(VFSDiskBackendTest, RenameMovesFile)
{
    WriteFile("/from.txt", "payload");

    ASSERT_TRUE(m_backend.Rename(VFSPath::Parse("/from.txt").Value(), VFSPath::Parse("/to.txt").Value()).Succeeded());
    EXPECT_FALSE(m_backend.Exists(VFSPath::Parse("/from.txt").Value()));
    EXPECT_TRUE(m_backend.Exists(VFSPath::Parse("/to.txt").Value()));
}

TEST_F(VFSDiskBackendTest, ExclusiveCreateNeverTruncatesAnExistingNameOrLeaksFileSlots)
{
    const auto path  = VFSPath::Parse("/existing.txt").Value();
    const auto flags = VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive;
    WriteFile(path.CStr(), "keep these bytes");
    for (int attempt = 0; attempt < 300; ++attempt)
    {
        auto result = m_backend.Open(path, flags | VFSOpenFlags::Truncate);
        ASSERT_TRUE(result.Failed());
        EXPECT_EQ(result.Error(), VFSError::AlreadyExists);
    }
    auto existing = m_backend.Open(path, VFSOpenFlags::Read);
    ASSERT_TRUE(existing.Succeeded());
    auto buffer = ReadBuffer(32);
    auto read   = existing.Value()->Read({buffer.data(), buffer.size()}, 0);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_TRUE(BytesEqual(buffer.data(), read.Value(), "keep these bytes"));
    m_backend.Close(existing.Value());
    const auto fresh   = VFSPath::Parse("/fresh.txt").Value();
    auto       created = m_backend.Open(fresh, flags);
    ASSERT_TRUE(created.Succeeded());
    m_backend.Close(created.Value());
    EXPECT_EQ(m_backend.Open(fresh, flags).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.Open(fresh, VFSOpenFlags::Exclusive | VFSOpenFlags::Read).Error(), VFSError::Unsupported);
}

TEST_F(VFSDiskBackendTest, ConcurrentExclusiveCreatesHaveExactlyOneWinner)
{
    const auto       path       = VFSPath::Parse("/contended.txt").Value();
    std::atomic<int> successes  = 0;
    std::atomic<int> conflicts  = 0;
    std::thread      workers[8] = {};
    for (auto& worker : workers)
        worker = std::thread([&]() {
            auto opened = m_backend.Open(path, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive);
            if (opened.Succeeded())
            {
                ++successes;
                m_backend.Close(opened.Value());
            }
            else if (opened.Error() == VFSError::AlreadyExists)
                ++conflicts;
        });
    for (auto& worker : workers)
        worker.join();
    EXPECT_EQ(successes.load(), 1);
    EXPECT_EQ(conflicts.load(), 7);
}

TEST_F(VFSDiskBackendTest, ExhaustedFilePoolFailsBeforeCreatingAFileAndRecovers)
{
    WriteFile("/existing.txt", "keep");
    IVFSFile* handles[kVFSMaxOpenFiles] = {};
    for (auto& handle : handles)
    {
        const auto opened = m_backend.Open(VFSPath::Parse("/existing.txt").Value(), VFSOpenFlags::Read);
        ASSERT_TRUE(opened.Succeeded());
        handle = opened.Value();
    }
    const auto fresh = VFSPath::Parse("/fresh.txt").Value();
    const auto flags = VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive;
    EXPECT_EQ(m_backend.Open(fresh, flags).Error(), VFSError::OutOfMemory);
    EXPECT_FALSE(m_backend.Exists(fresh));
    for (auto* handle : handles)
        m_backend.Close(handle);
    const auto opened = m_backend.Open(fresh, flags);
    ASSERT_TRUE(opened.Succeeded());
    m_backend.Close(opened.Value());
}

TEST_F(VFSDiskBackendTest, ConcurrentNoReplaceRenamesHaveExactlyOneWinner)
{
    VFSPath sources[8] = {};
    for (size_t i = 0; i < std::size(sources); ++i)
    {
        char path[32] = {};
        snprintf(path, sizeof(path), "/source-%zu.txt", i);
        sources[i] = VFSPath::Parse(path).Value();
        WriteFile(path, "keep");
    }
    const auto       target     = VFSPath::Parse("/winner.txt").Value();
    std::atomic<int> successes  = 0;
    std::atomic<int> conflicts  = 0;
    std::thread      workers[8] = {};
    for (size_t i = 0; i < std::size(workers); ++i)
        workers[i] = std::thread([&, i]() {
            const auto renamed = m_backend.RenameNoReplace(sources[i], target);
            if (renamed.Succeeded())
                ++successes;
            else if (renamed.Error() == VFSError::AlreadyExists)
                ++conflicts;
        });
    for (auto& worker : workers)
        worker.join();
    EXPECT_EQ(successes.load(), 1);
    EXPECT_EQ(conflicts.load(), 7);
    int retained = 0;
    for (const auto& source : sources)
        retained += m_backend.Exists(source) ? 1 : 0;
    EXPECT_EQ(retained, 7);
    const auto opened = m_backend.Open(target, VFSOpenFlags::Read);
    ASSERT_TRUE(opened.Succeeded());
    uint8_t buffer[4] = {};
    EXPECT_EQ(opened.Value()->Read({buffer, sizeof(buffer)}, 0).Value(), sizeof(buffer));
    EXPECT_TRUE(BytesEqual(buffer, sizeof(buffer), "keep"));
    m_backend.Close(opened.Value());
}

TEST_F(VFSDiskBackendTest, NoReplaceRenamePreservesConflictingFilesAndDirectories)
{
    const auto source = VFSPath::Parse("/source.txt").Value();
    const auto target = VFSPath::Parse("/target.txt").Value();
    WriteFile(source.CStr(), "source");
    WriteFile(target.CStr(), "target");
    EXPECT_EQ(m_backend.RenameNoReplace(source, target).Error(), VFSError::AlreadyExists);
    EXPECT_TRUE(m_backend.Exists(source));
    auto opened = m_backend.Open(target, VFSOpenFlags::Read);
    ASSERT_TRUE(opened.Succeeded());
    auto buffer = ReadBuffer(16);
    auto read   = opened.Value()->Read({buffer.data(), buffer.size()}, 0);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_TRUE(BytesEqual(buffer.data(), read.Value(), "target"));
    m_backend.Close(opened.Value());
    const auto directory = VFSPath::Parse("/directory").Value();
    ASSERT_TRUE(m_backend.CreateDir(directory).Succeeded());
    EXPECT_EQ(m_backend.RenameNoReplace(source, directory).Error(), VFSError::AlreadyExists);
    EXPECT_TRUE(m_backend.Stat(directory).Value().IsDirectory);
    const auto moved = VFSPath::Parse("/moved.txt").Value();
    ASSERT_TRUE(m_backend.RenameNoReplace(source, moved).Succeeded());
    EXPECT_FALSE(m_backend.Exists(source));
    EXPECT_TRUE(m_backend.Exists(moved));
    EXPECT_EQ(m_backend.RenameNoReplace(source, VFSPath::Parse("/missing.txt").Value()).Error(), VFSError::NotFound);
    EXPECT_EQ(m_backend.RenameNoReplace(VFSPath::Root(), moved).Error(), VFSError::InvalidPath);
}

TEST_F(VFSDiskBackendTest, NoReplaceDirectoryRenameMovesTheWholeSubtree)
{
    ASSERT_TRUE(m_backend.CreateDir(VFSPath::Parse("/old/child").Value()).Succeeded());
    WriteFile("/old/child/asset.txt", "asset");
    EXPECT_EQ(m_backend.RenameNoReplace(VFSPath::Parse("/old").Value(), VFSPath::Parse("/old/nested").Value()).Error(), VFSError::InvalidPath);
    ASSERT_TRUE(m_backend.RenameNoReplace(VFSPath::Parse("/old").Value(), VFSPath::Parse("/new").Value()).Succeeded());
    EXPECT_FALSE(m_backend.Exists(VFSPath::Parse("/old").Value()));
    EXPECT_TRUE(m_backend.Exists(VFSPath::Parse("/new/child/asset.txt").Value()));
}

TEST_F(VFSDiskBackendTest, CapabilitiesAndType)
{
    EXPECT_TRUE(HasCap(m_backend.Capabilities(), VFSBackendCaps::Write));
    EXPECT_STREQ(m_backend.BackendType(), "disk");
}

TEST_F(VFSDiskBackendTest, OpenWithCreateAndTruncate)
{
    const VFSPath path = VFSPath::Parse("/atomic.tmp").Value();
    ASSERT_FALSE(m_backend.Exists(path));

    // Write | Create — creates a non-existent file and writes content.
    {
        VFSResult<IVFSFile*> r = m_backend.Open(path, VFSOpenFlags::Write | VFSOpenFlags::Create);
        ASSERT_TRUE(r.Succeeded());
        IVFSFile*         file    = r.Value();
        cstring           payload = "hello create";
        const size_t      n       = secure_strlen(payload);
        VFSResult<size_t> wrote   = file->Write(ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(payload), n), 0);
        EXPECT_TRUE(wrote.Succeeded());
        EXPECT_EQ(wrote.Value(), n);
        m_backend.Close(file);
    }

    // Read back and verify content.
    {
        VFSResult<IVFSFile*> r = m_backend.Open(path, VFSOpenFlags::Read);
        ASSERT_TRUE(r.Succeeded());
        IVFSFile*         file   = r.Value();
        Array<uint8_t>    buffer = ReadBuffer(64);
        VFSResult<size_t> read   = file->Read(ArrayView<uint8_t>(buffer.data(), buffer.size()), 0);
        ASSERT_TRUE(read.Succeeded());
        EXPECT_TRUE(BytesEqual(buffer.data(), read.Value(), "hello create"));
        m_backend.Close(file);
    }

    // Write | Create | Truncate — reopens and clears the existing file.
    {
        VFSResult<IVFSFile*> r = m_backend.Open(path, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate);
        ASSERT_TRUE(r.Succeeded());
        IVFSFile*           file = r.Value();
        VFSResult<uint64_t> sz   = file->Size();
        ASSERT_TRUE(sz.Succeeded());
        EXPECT_EQ(sz.Value(), 0u);
        m_backend.Close(file);
    }
}

TEST_F(VFSDiskBackendTest, ReadOnlyBackendRejectsWrite)
{
    VFSDiskBackend read_only;
    read_only.Initialize(m_root.c_str(), VFSBackendCaps::Read, &m_manager.MainArena);

    VFSResult<IVFSFile*> r = read_only.Open(VFSPath::Parse("/ro.txt").Value(), VFSOpenFlags::Write);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::PermissionDenied);
}
