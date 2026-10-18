#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <atomic>
#include <limits>
#include <thread>
#include <vector>

using namespace ZEngine;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Core::Memory;
using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;

namespace
{
    ArrayView<const uint8_t> Bytes(const void* data, size_t size)
    {
        return ArrayView<const uint8_t>(static_cast<const uint8_t*>(data), size);
    }
} // namespace

class VFSMemoryBackendTest : public ::testing::Test
{
protected:
    MemoryManager    m_manager;
    ArenaAllocator*  m_arena = nullptr;
    VFSMemoryBackend m_backend;

    void             SetUp() override
    {
        m_manager.Initialize(ZMega(16), {});
        m_arena = &m_manager.MainArena;
        m_backend.Initialize(m_arena);
    }

    VFSPath P(const char* raw)
    {
        auto result = VFSPath::Parse(raw);
        EXPECT_TRUE(result.Succeeded()) << "failed to parse path: " << raw;
        return result.Value();
    }
};

TEST_F(VFSMemoryBackendTest, WriteFile_and_Read)
{
    const uint8_t png[] = {0x89, 0x50, 0x4e, 0x47};
    ASSERT_TRUE(m_backend.WriteFile(P("/tex/logo.png"), Bytes(png, sizeof(png))).Succeeded());

    auto open_result = m_backend.Open(P("/tex/logo.png"), VFSOpenFlags::Read);
    ASSERT_TRUE(open_result.Succeeded());
    IVFSFile* file      = open_result.Value();

    uint8_t   buffer[4] = {};
    auto      read      = file->Read(ArrayView<uint8_t>(buffer, sizeof(buffer)), 0);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 4u);
    EXPECT_EQ(0, Helpers::secure_memcmp(buffer, sizeof(buffer), png, sizeof(png), sizeof(png)));

    m_backend.Close(file);
}

TEST_F(VFSMemoryBackendTest, MemoryMap_ReturnsZeroCopyPointer)
{
    uint8_t pattern[1024];
    for (size_t i = 0; i < sizeof(pattern); ++i)
    {
        pattern[i] = static_cast<uint8_t>(i & 0xFF);
    }
    ASSERT_TRUE(m_backend.WriteFile(P("/data.bin"), Bytes(pattern, sizeof(pattern))).Succeeded());

    auto open_result = m_backend.Open(P("/data.bin"), VFSOpenFlags::Read);
    ASSERT_TRUE(open_result.Succeeded());
    auto*    file        = static_cast<VFSMemoryFile*>(open_result.Value());

    uint64_t mapped_size = 0;
    auto     mapped      = file->MemoryMap(mapped_size);
    ASSERT_TRUE(mapped.Succeeded());
    EXPECT_EQ(mapped_size, 1024u);
    EXPECT_EQ(0, Helpers::secure_memcmp(mapped.Value(), mapped_size, pattern, sizeof(pattern), sizeof(pattern)));

    m_backend.Close(file);
}

TEST_F(VFSMemoryBackendTest, WriteFile_Overwrites_Existing)
{
    ASSERT_TRUE(m_backend.WriteFile(P("/a.txt"), Bytes("hello", 5)).Succeeded());
    ASSERT_TRUE(m_backend.WriteFile(P("/a.txt"), Bytes("world", 5)).Succeeded());

    auto open_result = m_backend.Open(P("/a.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(open_result.Succeeded());
    IVFSFile* file      = open_result.Value();

    char      buffer[5] = {};
    auto      read      = file->Read(ArrayView<uint8_t>(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer)), 0);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 5u);
    EXPECT_EQ(0, Helpers::secure_memcmp(buffer, sizeof(buffer), "world", 5, 5));

    m_backend.Close(file);
}

TEST_F(VFSMemoryBackendTest, Open_Write_Accumulates_And_Commits)
{
    auto open_write = m_backend.Open(P("/b.txt"), VFSOpenFlags::Write);
    ASSERT_TRUE(open_write.Succeeded());
    IVFSFile*     writer   = open_write.Value();

    const uint8_t first[]  = {1, 2, 3};
    const uint8_t second[] = {4, 5};
    ASSERT_TRUE(writer->Write(Bytes(first, sizeof(first)), 0).Succeeded());
    ASSERT_TRUE(writer->Write(Bytes(second, sizeof(second)), 3).Succeeded());
    ASSERT_TRUE(writer->Flush().Succeeded());
    m_backend.Close(writer);

    auto open_read = m_backend.Open(P("/b.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(open_read.Succeeded());
    IVFSFile* reader    = open_read.Value();

    uint8_t   buffer[5] = {};
    auto      read      = reader->Read(ArrayView<uint8_t>(buffer, sizeof(buffer)), 0);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 5u);
    const uint8_t expected[] = {1, 2, 3, 4, 5};
    EXPECT_EQ(0, Helpers::secure_memcmp(buffer, sizeof(buffer), expected, sizeof(expected), sizeof(expected)));

    m_backend.Close(reader);
}

TEST_F(VFSMemoryBackendTest, RepeatedFlushesReuseCommittedStorageAndCloseDoesNotCommitAgain)
{
    const auto path   = P("/flush.bin");
    const auto opened = m_backend.Open(path, VFSOpenFlags::Write);
    ASSERT_TRUE(opened.Succeeded());
    auto*         writer     = opened.Value();
    const uint8_t initial[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_TRUE(writer->Write(Bytes(initial, sizeof(initial)), 0).Succeeded());
    ASSERT_TRUE(writer->Flush().Succeeded());
    const auto committed_bytes = m_arena->m_current_offset;
    for (int i = 0; i < 32; ++i)
    {
        ASSERT_TRUE(writer->Flush().Succeeded());
        EXPECT_EQ(m_arena->m_current_offset, committed_bytes);
        const uint8_t value = static_cast<uint8_t>(i);
        ASSERT_TRUE(writer->Write(Bytes(&value, 1), 0).Succeeded());
        ASSERT_TRUE(writer->Flush().Succeeded());
        EXPECT_EQ(m_arena->m_current_offset, committed_bytes);
    }
    m_backend.Close(writer);
    EXPECT_EQ(m_arena->m_current_offset, committed_bytes);
    const auto read = m_backend.Open(path, VFSOpenFlags::Read);
    ASSERT_TRUE(read.Succeeded());
    uint8_t bytes[sizeof(initial)] = {};
    ASSERT_EQ(read.Value()->Read(ArrayView<uint8_t>(bytes, sizeof(bytes)), 0).Value(), sizeof(bytes));
    EXPECT_EQ(bytes[0], 31);
    for (size_t i = 1; i < sizeof(bytes); ++i)
        EXPECT_EQ(bytes[i], initial[i]);
    m_backend.Close(read.Value());
}

TEST_F(VFSMemoryBackendTest, WholeFileOverwritesReuseStorageAndZeroLengthWritesTruncate)
{
    const auto path = P("/overwrite.txt");
    ASSERT_TRUE(m_backend.WriteFile(path, Bytes("original", 8)).Succeeded());
    const auto arena_bytes = m_arena->m_current_offset;
    for (int i = 0; i < 32; ++i)
    {
        ASSERT_TRUE(m_backend.WriteFile(path, Bytes("next", 4)).Succeeded());
        EXPECT_EQ(m_backend.Stat(path).Value().SizeBytes, 4u);
        ASSERT_TRUE(m_backend.WriteFile(path, Bytes(nullptr, 0)).Succeeded());
        EXPECT_EQ(m_backend.Stat(path).Value().SizeBytes, 0u);
        EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
    }
    ASSERT_TRUE(m_backend.WriteFile(path, Bytes("restored", 8)).Succeeded());
    EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
    const auto read = m_backend.Open(path, VFSOpenFlags::Read);
    ASSERT_TRUE(read.Succeeded());
    uint8_t bytes[8] = {};
    ASSERT_EQ(read.Value()->Read(ArrayView<uint8_t>(bytes, sizeof(bytes)), 0).Value(), sizeof(bytes));
    EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), "restored", 8, 8), 0);
    m_backend.Close(read.Value());
}

TEST_F(VFSMemoryBackendTest, SparseWritesZeroOnlyGapsAndPreserveBytesAcrossGrowthAndTruncation)
{
    const auto path   = P("/sparse.bin");
    auto       opened = m_backend.Open(path, VFSOpenFlags::Write);
    ASSERT_TRUE(opened.Succeeded());
    auto* writer = opened.Value();
    ASSERT_TRUE(writer->Write(Bytes("abc", 3), 0).Succeeded());
    ASSERT_TRUE(writer->Write(Bytes("z", 1), 40).Succeeded());
    ASSERT_TRUE(writer->Write(Bytes("d", 1), 3).Succeeded());
    ASSERT_TRUE(writer->Flush().Succeeded());
    m_backend.Close(writer);
    opened = m_backend.Open(path, VFSOpenFlags::Read);
    ASSERT_TRUE(opened.Succeeded());
    uint8_t bytes[41] = {};
    ASSERT_EQ(opened.Value()->Read(ArrayView<uint8_t>(bytes, sizeof(bytes)), 0).Value(), sizeof(bytes));
    EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), "abcd", 4, 4), 0);
    for (size_t i = 4; i < 40; ++i)
        EXPECT_EQ(bytes[i], 0);
    EXPECT_EQ(bytes[40], 'z');
    m_backend.Close(opened.Value());
    const auto arena_bytes = m_arena->m_current_offset;
    opened                 = m_backend.Open(path, VFSOpenFlags::Write | VFSOpenFlags::Truncate);
    ASSERT_TRUE(opened.Succeeded());
    const auto writer_bytes = m_arena->m_current_offset;
    m_backend.Close(opened.Value());
    EXPECT_EQ(m_arena->m_current_offset, writer_bytes);
    EXPECT_EQ(m_backend.Stat(path).Value().SizeBytes, 0u);
    EXPECT_GE(writer_bytes, arena_bytes);
}

TEST_F(VFSMemoryBackendTest, InterleavedSmallWritesGrowScratchStorageGeometrically)
{
    const auto opened = m_backend.Open(P("/stream.bin"), VFSOpenFlags::Write);
    ASSERT_TRUE(opened.Succeeded());
    auto*            writer      = opened.Value();
    constexpr size_t count       = 256;
    const auto       arena_bytes = m_arena->m_current_offset;
    for (size_t i = 0; i < count; ++i)
    {
        const uint8_t value = static_cast<uint8_t>(i);
        ASSERT_TRUE(writer->Write(Bytes(&value, 1), i).Succeeded());
        ASSERT_NE(m_arena->Allocate(1, 1), nullptr); // Prevent in-place growth of the write buffer.
    }
    EXPECT_LE(m_arena->m_current_offset - arena_bytes, count * 3);
    EXPECT_EQ(writer->Size().Value(), count);
    m_backend.Close(writer);
    const auto read = m_backend.Open(P("/stream.bin"), VFSOpenFlags::Read);
    ASSERT_TRUE(read.Succeeded());
    uint8_t bytes[count] = {};
    ASSERT_EQ(read.Value()->Read(ArrayView<uint8_t>(bytes, sizeof(bytes)), 0).Value(), count);
    for (size_t i = 0; i < count; ++i)
        EXPECT_EQ(bytes[i], static_cast<uint8_t>(i));
    m_backend.Close(read.Value());
}

TEST_F(VFSMemoryBackendTest, CreateDir_and_List)
{
    ASSERT_TRUE(m_backend.CreateDir(P("/assets/textures")).Succeeded());
    ASSERT_TRUE(m_backend.WriteFile(P("/assets/textures/a.png"), Bytes(nullptr, 0)).Succeeded());
    ASSERT_TRUE(m_backend.WriteFile(P("/assets/textures/b.png"), Bytes(nullptr, 0)).Succeeded());

    auto listing = m_backend.List(m_arena, P("/assets/textures"));
    ASSERT_TRUE(listing.Succeeded());
    EXPECT_EQ(listing.Value().size(), 2u);
}

TEST_F(VFSMemoryBackendTest, Remove_File)
{
    const uint8_t byte = 0;
    ASSERT_TRUE(m_backend.WriteFile(P("/tmp.bin"), Bytes(&byte, 1)).Succeeded());
    ASSERT_TRUE(m_backend.Remove(P("/tmp.bin")).Succeeded());

    auto open_result = m_backend.Open(P("/tmp.bin"), VFSOpenFlags::Read);
    EXPECT_TRUE(open_result.Failed());
    EXPECT_EQ(open_result.Error(), VFSError::NotFound);
}

TEST_F(VFSMemoryBackendTest, Rename_File)
{
    const uint8_t data[] = {1, 2};
    ASSERT_TRUE(m_backend.WriteFile(P("/old.txt"), Bytes(data, sizeof(data))).Succeeded());
    ASSERT_TRUE(m_backend.Rename(P("/old.txt"), P("/new.txt")).Succeeded());

    EXPECT_TRUE(m_backend.Open(P("/old.txt"), VFSOpenFlags::Read).Failed());

    auto open_new = m_backend.Open(P("/new.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(open_new.Succeeded());
    IVFSFile* file = open_new.Value();
    EXPECT_EQ(file->Size().Value(), 2u);
    m_backend.Close(file);
}

TEST_F(VFSMemoryBackendTest, ExclusiveCreateRejectsConflictsWithoutChangingContents)
{
    const auto path = P("/keep.txt");
    ASSERT_TRUE(m_backend.WriteFile(path, Bytes("keep", 4)).Succeeded());
    const auto flags = VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive;
    EXPECT_EQ(m_backend.Open(path, flags | VFSOpenFlags::Truncate).Error(), VFSError::AlreadyExists);
    auto existing = m_backend.Open(path, VFSOpenFlags::Read);
    ASSERT_TRUE(existing.Succeeded());
    uint8_t bytes[4] = {};
    EXPECT_EQ(existing.Value()->Read({bytes, sizeof(bytes)}, 0).Value(), sizeof(bytes));
    EXPECT_EQ(Helpers::secure_memcmp(bytes, sizeof(bytes), "keep", 4, 4), 0);
    m_backend.Close(existing.Value());
    auto created = m_backend.Open(P("/new.txt"), flags);
    ASSERT_TRUE(created.Succeeded());
    m_backend.Close(created.Value());
    EXPECT_EQ(m_backend.Open(P("/new.txt"), flags).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.Open(path, VFSOpenFlags::Exclusive).Error(), VFSError::Unsupported);
}

TEST_F(VFSMemoryBackendTest, ImplicitRootIsAlwaysAnExistingDirectory)
{
    const auto root = VFSPath::Root();
    const auto stat = m_backend.Stat(root);
    ASSERT_TRUE(stat.Succeeded());
    EXPECT_TRUE(stat.Value().IsDirectory);
    EXPECT_FALSE(stat.Value().IsReadOnly);
    EXPECT_EQ(stat.Value().SizeBytes, 0u);
    EXPECT_EQ(stat.Value().MTimeNs, 0);
    EXPECT_TRUE(m_backend.Exists(root));
    EXPECT_EQ(m_backend.CreateDir(root).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.Open(root, VFSOpenFlags::Write).Error(), VFSError::NotAFile);
    EXPECT_EQ(m_backend.Open(root, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.WriteFile(root, Bytes("no", 2)).Error(), VFSError::NotAFile);
    EXPECT_EQ(m_backend.Stat(VFSPath{}).Error(), VFSError::InvalidPath);
    EXPECT_FALSE(m_backend.Exists(VFSPath{}));
}

TEST_F(VFSMemoryBackendTest, ExhaustedFilePoolDoesNotPublishAnExclusiveNameOrGrowTheArena)
{
    IVFSFile*  handles[kVFSMaxOpenMemoryFiles] = {};
    const auto existing                        = P("/existing.txt");
    for (auto& handle : handles)
    {
        const auto opened = m_backend.Open(existing, VFSOpenFlags::Write);
        ASSERT_TRUE(opened.Succeeded());
        handle = opened.Value();
    }
    const auto arena_bytes = m_arena->m_current_offset;
    const auto path        = P("/new/fresh.txt");
    const auto flags       = VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Exclusive;
    EXPECT_EQ(m_backend.Open(path, flags).Error(), VFSError::OutOfMemory);
    EXPECT_FALSE(m_backend.Exists(path));
    EXPECT_FALSE(m_backend.Exists(P("/new")));
    EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
    for (auto* handle : handles)
        m_backend.Close(handle);
    const auto opened = m_backend.Open(path, flags);
    ASSERT_TRUE(opened.Succeeded());
    m_backend.Close(opened.Value());
}

TEST_F(VFSMemoryBackendTest, ExclusiveDirectoryCreationDoesNotPublishMissingParentsOrReplaceAnExistingName)
{
    const auto path = P("/exclusive");
    ASSERT_TRUE(m_backend.CreateDirExclusive(path).Succeeded());
    ASSERT_TRUE(m_backend.WriteFile(P("/exclusive/keep.txt"), Bytes("keep", 4)).Succeeded());
    const auto arena_bytes = m_arena->m_current_offset;
    EXPECT_EQ(m_backend.CreateDirExclusive(path).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.CreateDirExclusive(P("/exclusive/keep.txt")).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.CreateDirExclusive(P("/absent/child")).Error(), VFSError::NotFound);
    EXPECT_EQ(m_backend.CreateDirExclusive(P("/exclusive/keep.txt/child")).Error(), VFSError::NotADirectory);
    EXPECT_EQ(m_backend.CreateDirExclusive(VFSPath{}).Error(), VFSError::InvalidPath);
    EXPECT_EQ(m_backend.CreateDirExclusive(VFSPath::Root()).Error(), VFSError::AlreadyExists);
    EXPECT_FALSE(m_backend.Exists(P("/absent")));
    EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
    EXPECT_EQ(m_backend.Stat(P("/exclusive/keep.txt")).Value().SizeBytes, 4u);
    EXPECT_TRUE(m_backend.CreateDirExclusive(P("/exclusive/child")).Succeeded());
}

TEST_F(VFSMemoryBackendTest, WriteRejectsOffsetOverflowWithoutAllocatingOrChangingTheFile)
{
    const auto path = P("/offset.txt");
    ASSERT_TRUE(m_backend.WriteFile(path, Bytes("keep", 4)).Succeeded());
    const auto opened = m_backend.Open(path, VFSOpenFlags::Write | VFSOpenFlags::Append);
    ASSERT_TRUE(opened.Succeeded());
    const auto arena_bytes = m_arena->m_current_offset;
    EXPECT_EQ(opened.Value()->Write(Bytes("x", 1), std::numeric_limits<uint64_t>::max()).Error(), VFSError::SizeLimitExceeded);
    const auto empty = opened.Value()->Write(Bytes(nullptr, 0), std::numeric_limits<uint64_t>::max());
    ASSERT_TRUE(empty.Succeeded());
    EXPECT_EQ(empty.Value(), 0u);
    EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
    EXPECT_EQ(opened.Value()->Size().Value(), 4u);
    m_backend.Close(opened.Value());
    EXPECT_EQ(m_backend.Stat(path).Value().SizeBytes, 4u);
}

TEST_F(VFSMemoryBackendTest, NoReplaceRenameMovesDescendantsAndDoesNotGrowTheArena)
{
    ASSERT_TRUE(m_backend.CreateDir(P("/old/child")).Succeeded());
    ASSERT_TRUE(m_backend.WriteFile(P("/old/child/asset.txt"), Bytes("asset", 5)).Succeeded());
    ASSERT_TRUE(m_backend.WriteFile(P("/keep.txt"), Bytes("keep", 4)).Succeeded());
    const auto arena_bytes = m_arena->m_current_offset;
    EXPECT_EQ(m_backend.RenameNoReplace(P("/old"), P("/keep.txt")).Error(), VFSError::AlreadyExists);
    EXPECT_EQ(m_backend.RenameNoReplace(P("/old"), P("/old/nested")).Error(), VFSError::InvalidPath);
    EXPECT_EQ(m_backend.RenameNoReplace(P("/old"), P("/missing/child")).Error(), VFSError::NotFound);
    for (int iteration = 0; iteration < 64; ++iteration)
    {
        ASSERT_TRUE(m_backend.RenameNoReplace(P("/old"), P("/new")).Succeeded());
        EXPECT_FALSE(m_backend.Exists(P("/old/child/asset.txt")));
        EXPECT_TRUE(m_backend.Exists(P("/new/child/asset.txt")));
        ASSERT_TRUE(m_backend.RenameNoReplace(P("/new"), P("/old")).Succeeded());
        EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
    }
    ASSERT_TRUE(m_backend.RenameNoReplace(P("/keep.txt"), P("/renamed.txt")).Succeeded());
    EXPECT_TRUE(m_backend.Exists(P("/renamed.txt")));
    EXPECT_FALSE(m_backend.Exists(P("/keep.txt")));
    EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
}

TEST_F(VFSMemoryBackendTest, NoReplaceDirectoryRenameValidatesEveryDescendantBeforeMutation)
{
    char long_path[VFS_MAX_PATH] = {};
    Helpers::secure_memset(long_path, 'x', sizeof(long_path) - 1, sizeof(long_path));
    long_path[0] = '/';
    long_path[1] = 'a';
    long_path[2] = '/';
    ASSERT_TRUE(m_backend.WriteFile(P(long_path), Bytes("keep", 4)).Succeeded());
    EXPECT_EQ(m_backend.RenameNoReplace(P("/a"), P("/longer")).Error(), VFSError::InvalidPath);
    EXPECT_TRUE(m_backend.Exists(P("/a")));
    EXPECT_TRUE(m_backend.Exists(P(long_path)));
    EXPECT_FALSE(m_backend.Exists(P("/longer")));
}

TEST_F(VFSMemoryBackendTest, NoReplaceDirectoryRenamePreservesAllDescendantsAndUnrelatedPrefixes)
{
    ASSERT_TRUE(m_backend.CreateDir(P("/old/child")).Succeeded());
    ASSERT_TRUE(m_backend.WriteFile(P("/oldish/keep.txt"), Bytes("keep", 4)).Succeeded());
    for (int i = 0; i < 40; ++i)
    {
        char path[VFS_MAX_PATH] = {};
        snprintf(path, sizeof(path), "/old/child/asset-%d.txt", i);
        ASSERT_TRUE(m_backend.WriteFile(P(path), Bytes("asset", 5)).Succeeded());
    }
    const auto arena_bytes = m_arena->m_current_offset;
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        ASSERT_TRUE(m_backend.RenameNoReplace(P("/old"), P("/renamed")).Succeeded());
        for (int i = 0; i < 40; ++i)
        {
            char path[VFS_MAX_PATH] = {};
            snprintf(path, sizeof(path), "/renamed/child/asset-%d.txt", i);
            const auto stat = m_backend.Stat(P(path));
            ASSERT_TRUE(stat.Succeeded());
            EXPECT_EQ(stat.Value().SizeBytes, 5u);
        }
        EXPECT_FALSE(m_backend.Exists(P("/old")));
        EXPECT_TRUE(m_backend.Exists(P("/oldish/keep.txt")));
        ASSERT_TRUE(m_backend.RenameNoReplace(P("/renamed"), P("/old")).Succeeded());
        EXPECT_EQ(m_arena->m_current_offset, arena_bytes);
    }
}

TEST_F(VFSMemoryBackendTest, Capabilities)
{
    const VFSBackendCaps caps = m_backend.Capabilities();
    EXPECT_TRUE(HasCap(caps, VFSBackendCaps::Read));
    EXPECT_TRUE(HasCap(caps, VFSBackendCaps::Write));
    EXPECT_TRUE(HasCap(caps, VFSBackendCaps::List));
    EXPECT_TRUE(HasCap(caps, VFSBackendCaps::MemoryMap));
}

TEST_F(VFSMemoryBackendTest, ConcurrentReads_NoDeadlock)
{
    Array<uint8_t> data;
    data.init(m_arena, 4096, 4096);
    for (size_t i = 0; i < data.size(); ++i)
    {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    ASSERT_TRUE(m_backend.WriteFile(P("/shared.bin"), Bytes(data.data(), data.size())).Succeeded());

    std::atomic<int>         success{0};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back([&] {
            auto open_result = m_backend.Open(P("/shared.bin"), VFSOpenFlags::Read);
            if (!open_result.Succeeded())
            {
                return;
            }
            IVFSFile* file         = open_result.Value();

            uint8_t   buffer[4096] = {};
            auto      read         = file->Read(ArrayView<uint8_t>(buffer, sizeof(buffer)), 0);
            if (read.Succeeded() && read.Value() == 4096u)
            {
                ++success;
            }
            m_backend.Close(file);
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    EXPECT_EQ(success.load(), 8);
}
