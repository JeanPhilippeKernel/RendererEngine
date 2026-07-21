#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>

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
} // namespace

class VFSDiskBackendTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_manager.Initialize({.BufferSize = ZKilo(256)});

        m_root.init(&m_manager.MainArena, (std::filesystem::temp_directory_path() / "zengine_vfs_disk_tests").string().c_str());

        std::error_code ec;
        std::filesystem::remove_all(m_root.c_str(), ec);
        std::filesystem::create_directories(m_root.c_str(), ec);

        m_backend.Initialize(m_root.c_str(), VFSBackendCaps::Read | VFSBackendCaps::Write, &m_manager.MainArena);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root.c_str(), ec);
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

    String         m_root;
    MemoryManager  m_manager{};
    VFSDiskBackend m_backend;
};

TEST_F(VFSDiskBackendTest, OpenMissingFileFailsWithNotFound)
{
    VFSResult<IVFSFile*> r = m_backend.Open(VFSPath::Parse("/nope.txt").Value(), VFSOpenFlags::Read);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::NotFound);
}

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

TEST_F(VFSDiskBackendTest, CapabilitiesAndType)
{
    EXPECT_TRUE(HasCap(m_backend.Capabilities(), VFSBackendCaps::Write));
    EXPECT_STREQ(m_backend.BackendType(), "disk");
}

TEST_F(VFSDiskBackendTest, ReadOnlyBackendRejectsWrite)
{
    VFSDiskBackend read_only;
    read_only.Initialize(m_root.c_str(), VFSBackendCaps::Read, &m_manager.MainArena);

    VFSResult<IVFSFile*> r = read_only.Open(VFSPath::Parse("/ro.txt").Value(), VFSOpenFlags::Write);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::PermissionDenied);
}
