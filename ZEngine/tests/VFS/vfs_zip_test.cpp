#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSZipBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>

using namespace ZEngine::Core::VFS;
using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Containers::String;
using ZEngine::Core::Memory::ArenaAllocator;
using ZEngine::Core::Memory::MemoryManager;
using ZEngine::Helpers::secure_memcmp;
using ZEngine::Helpers::secure_strlen;

namespace
{
    VFSPath P(const char* s)
    {
        return VFSPath::Parse(s).Value();
    }

    bool BytesEqual(const uint8_t* data, size_t len, cstring expected)
    {
        const size_t elen = secure_strlen(expected);
        return len == elen && secure_memcmp(data, len, expected, elen, len) == 0;
    }

    bool FileContentEquals(ArenaAllocator* arena, IVFSFile* file, cstring expected)
    {
        const size_t        n    = secure_strlen(expected);
        VFSResult<uint64_t> size = file->Size();
        if (size.Failed() || size.Value() != n)
        {
            return false;
        }
        Array<uint8_t> buf;
        buf.init(arena, n ? n : 1, n);
        VFSResult<size_t> read = file->Read(ArrayView<uint8_t>(buf.data(), buf.size()), 0);
        return read.Succeeded() && read.Value() == n && BytesEqual(buf.data(), read.Value(), expected);
    }
} // namespace

class VFSZipBackendTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_manager.Initialize({.BufferSize = ZKilo(512)});

        m_archive.init(&m_manager.MainArena, (std::filesystem::temp_directory_path() / "zengine_vfs_zip_tests.zip").string().c_str());

        m_big.init(&m_manager.MainArena, 1025);
        for (int i = 0; i < 1024; ++i)
        {
            m_big.append('A');
        }

        std::error_code ec;
        std::filesystem::remove(m_archive.c_str(), ec);

        const VFSZipBackend::ZipFileSpec files[] = {
            {         "hello.txt",     "hello zip",            9},
            { "textures/rock.png",  "PNGDATA-rock",           12},
            {"textures/Grass.PNG", "PNGDATA-grass",           13},
            {  "shaders/pbr.vert", "void main(){}",           13},
            {           "big.txt",    m_big.data(), m_big.size()},
        };
        ASSERT_TRUE(VFSZipBackend::WriteArchive(m_archive.c_str(), files, sizeof(files) / sizeof(files[0])));

        m_backend.Initialize(m_archive.c_str(), &m_manager.MainArena /* case_sensitive = false */);
    }

    void TearDown() override
    {
        m_backend.Shutdown();
        std::error_code ec;
        std::filesystem::remove(m_archive.c_str(), ec);
        m_manager.Shutdown();
    }

    String        m_archive;
    String        m_big;
    MemoryManager m_manager{};
    VFSZipBackend m_backend;
};

TEST_F(VFSZipBackendTest, OpenKnownFileReadsContents)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/hello.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    EXPECT_TRUE(FileContentEquals(&m_manager.MainArena, r.Value(), "hello zip"));
    m_backend.Close(r.Value());
}

TEST_F(VFSZipBackendTest, OpenCaseInsensitiveLookup)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/TEXTURES/ROCK.PNG"), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    EXPECT_TRUE(FileContentEquals(&m_manager.MainArena, r.Value(), "PNGDATA-rock"));
    m_backend.Close(r.Value());
}

TEST_F(VFSZipBackendTest, OpenNonExistentFileReturnsNotFound)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/does/not/exist.txt"), VFSOpenFlags::Read);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::NotFound);
}

TEST_F(VFSZipBackendTest, OpenWriteAttemptReturnsPermissionDenied)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/hello.txt"), VFSOpenFlags::Write);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::PermissionDenied);
}

TEST_F(VFSZipBackendTest, OpenDirectoryReturnsNotAFile)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/textures"), VFSOpenFlags::Read);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::NotAFile);
}

TEST_F(VFSZipBackendTest, ReadAtOffset)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/hello.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file      = r.Value();

    uint8_t           buffer[3] = {};
    VFSResult<size_t> read      = file->Read(ArrayView<uint8_t>(buffer, 3), 6);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 3u);
    EXPECT_TRUE(BytesEqual(buffer, read.Value(), "zip"));
    m_backend.Close(file);
}

TEST_F(VFSZipBackendTest, ReadPastEofReturnsZero)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/hello.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file      = r.Value();

    uint8_t           buffer[4] = {};
    VFSResult<size_t> read      = file->Read(ArrayView<uint8_t>(buffer, 4), 1000);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 0u);
    m_backend.Close(file);
}

TEST_F(VFSZipBackendTest, CompressedEntryDecompressesCorrectly)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/big.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile* file = r.Value();

    EXPECT_EQ(file->Size().Value(), m_big.size());
    EXPECT_TRUE(FileContentEquals(&m_manager.MainArena, file, m_big.c_str()));
    m_backend.Close(file);
}

TEST_F(VFSZipBackendTest, StatExistingFileReportsUncompressedSize)
{
    VFSResult<VFSFileStat> stat = m_backend.Stat(P("/big.txt"));
    ASSERT_TRUE(stat.Succeeded());
    EXPECT_EQ(stat.Value().SizeBytes, m_big.size());
    EXPECT_FALSE(stat.Value().IsDirectory);
    EXPECT_TRUE(stat.Value().IsReadOnly);
}

TEST_F(VFSZipBackendTest, Exists)
{
    EXPECT_TRUE(m_backend.Exists(P("/shaders/pbr.vert")));
    EXPECT_TRUE(m_backend.Exists(P("/textures")));
    EXPECT_FALSE(m_backend.Exists(P("/nope.txt")));
}

TEST_F(VFSZipBackendTest, ListRootDirectory)
{
    VFSResult<Array<VFSDirEntry>> r = m_backend.List(&m_manager.MainArena, VFSPath::Root());
    ASSERT_TRUE(r.Succeeded());

    bool has_hello = false, has_textures = false, has_shaders = false, has_big = false;
    for (const VFSDirEntry& e : r.Value())
    {
        has_hello    = has_hello || e.Path.Filename().Equals("hello.txt");
        has_big      = has_big || e.Path.Filename().Equals("big.txt");
        has_textures = has_textures || (e.Path.Filename().Equals("textures") && e.IsDirectory);
        has_shaders  = has_shaders || (e.Path.Filename().Equals("shaders") && e.IsDirectory);
    }
    EXPECT_TRUE(has_hello);
    EXPECT_TRUE(has_big);
    EXPECT_TRUE(has_textures);
    EXPECT_TRUE(has_shaders);
}

TEST_F(VFSZipBackendTest, ListSubdirectory)
{
    VFSResult<Array<VFSDirEntry>> r = m_backend.List(&m_manager.MainArena, P("/textures"));
    ASSERT_TRUE(r.Succeeded());
    EXPECT_EQ(r.Value().size(), 2u);

    bool has_rock = false, has_grass = false;
    for (const VFSDirEntry& e : r.Value())
    {
        has_rock  = has_rock || e.Path.Filename().Equals("rock.png");
        has_grass = has_grass || e.Path.Filename().Equals("Grass.PNG");
    }
    EXPECT_TRUE(has_rock);
    EXPECT_TRUE(has_grass);
}

TEST_F(VFSZipBackendTest, ReReadServesFromDecompressCache)
{
    VFSResult<IVFSFile*> r = m_backend.Open(P("/big.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*      file = r.Value();

    const size_t   n    = m_big.size();
    Array<uint8_t> first, second;
    first.init(&m_manager.MainArena, n, n);
    second.init(&m_manager.MainArena, n, n);

    VFSResult<size_t> r1 = file->Read(ArrayView<uint8_t>(first.data(), first.size()), 0);
    VFSResult<size_t> r2 = file->Read(ArrayView<uint8_t>(second.data(), second.size()), 0);
    ASSERT_TRUE(r1.Succeeded());
    ASSERT_TRUE(r2.Succeeded());
    EXPECT_EQ(r1.Value(), n);
    EXPECT_EQ(r2.Value(), n);
    EXPECT_TRUE(BytesEqual(first.data(), r1.Value(), m_big.c_str()));
    EXPECT_TRUE(BytesEqual(second.data(), r2.Value(), m_big.c_str()));

    m_backend.Close(file);
}

TEST_F(VFSZipBackendTest, CapabilitiesAndType)
{
    EXPECT_STREQ(m_backend.BackendType(), "zip");
    EXPECT_TRUE(HasCap(m_backend.Capabilities(), VFSBackendCaps::Read));
    EXPECT_TRUE(HasCap(m_backend.Capabilities(), VFSBackendCaps::List));
    EXPECT_FALSE(HasCap(m_backend.Capabilities(), VFSBackendCaps::Write));
}

TEST(VFSContextOverlayTest, ZipOverridesLowerPriorityDisk)
{
    MemoryManager manager{};
    manager.Initialize({.BufferSize = ZKilo(512)});

    String disk_root;
    disk_root.init(&manager.MainArena, (std::filesystem::temp_directory_path() / "zengine_vfs_overlay_disk").string().c_str());
    String archive;
    archive.init(&manager.MainArena, (std::filesystem::temp_directory_path() / "zengine_vfs_overlay.zip").string().c_str());

    std::error_code ec;
    std::filesystem::remove_all(disk_root.c_str(), ec);
    std::filesystem::create_directories(disk_root.c_str(), ec);
    std::filesystem::remove(archive.c_str(), ec);

    VFSDiskBackend disk;
    disk.Initialize(disk_root.c_str(), VFSBackendCaps::Read | VFSBackendCaps::Write, &manager.MainArena);
    ASSERT_TRUE(disk.CreateDir(P("/assets")).Succeeded());
    {
        VFSResult<IVFSFile*> f = disk.Open(P("/assets/foo.txt"), VFSOpenFlags::Write);
        ASSERT_TRUE(f.Succeeded());
        const char disk_bytes[] = "from-disk";
        ASSERT_TRUE(f.Value()->Write(ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(disk_bytes), 9), 0).Succeeded());
        disk.Close(f.Value());
    }

    const VFSZipBackend::ZipFileSpec files[] = {
        {"foo.txt", "from-zip", 8}
    };
    ASSERT_TRUE(VFSZipBackend::WriteArchive(archive.c_str(), files, 1));
    VFSZipBackend zip;
    zip.Initialize(archive.c_str(), &manager.MainArena);

    VFSContext ctx;
    ctx.Initialize(&manager.MainArena, 8);
    ASSERT_TRUE(ctx.Mount(&disk, VFSPath::Root(), 0).Succeeded());
    ASSERT_TRUE(ctx.Mount(&zip, P("/assets"), 10).Succeeded());

    VFSResult<IVFSFile*> opened = ctx.Open(P("/assets/foo.txt"), VFSOpenFlags::Read);
    ASSERT_TRUE(opened.Succeeded());
    EXPECT_TRUE(FileContentEquals(&manager.MainArena, opened.Value(), "from-zip"));
    ctx.Close(opened.Value());

    zip.Shutdown();
    std::filesystem::remove_all(disk_root.c_str(), ec);
    std::filesystem::remove(archive.c_str(), ec);
    manager.Shutdown();
}
