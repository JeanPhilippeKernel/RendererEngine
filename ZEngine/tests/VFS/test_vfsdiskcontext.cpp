#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace ZEngine::Core::VFS;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Memory::MemoryManager;

class VFSDiskFileTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::error_code ec;
        m_root = (std::filesystem::temp_directory_path() / "zengine_vfs_tests").string();
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    void WriteFile(VFSDiskContext& ctx, const char* vfs_path, const std::string& data)
    {
        VFSResult<IVFSFile*> opened = ctx.Open(VFSPath::Parse(vfs_path).Value(), VFSOpenFlags::Write);
        ASSERT_TRUE(opened.Succeeded());

        IVFSFile*         file  = opened.Value();
        VFSResult<size_t> wrote = file->Write(ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(data.data()), data.size()), 0);
        EXPECT_TRUE(wrote.Succeeded());
        EXPECT_EQ(wrote.Value(), data.size());
        ctx.Close(file);
    }

    std::string m_root;
};

TEST_F(VFSDiskFileTest, OpenMissingFileFailsWithNotFound)
{
    VFSDiskContext       ctx(m_root.c_str());
    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/nope.txt").Value(), VFSOpenFlags::Read);
    EXPECT_TRUE(r.Failed());
    EXPECT_EQ(r.Error(), VFSError::NotFound);
}

TEST_F(VFSDiskFileTest, WriteThenReadBack)
{
    VFSDiskContext    ctx(m_root.c_str());
    const std::string contents = "hello vfs world";
    WriteFile(ctx, "/greeting.txt", contents);

    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/greeting.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*            file = r.Value();

    std::vector<uint8_t> buffer(contents.size());
    VFSResult<size_t>    read = file->Read(ArrayView<uint8_t>(buffer.data(), buffer.size()), 0);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), contents.size());
    EXPECT_EQ(std::string(buffer.begin(), buffer.end()), contents);

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, ReadAtOffset)
{
    VFSDiskContext ctx(m_root.c_str());
    WriteFile(ctx, "/data.bin", "0123456789");

    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/data.bin").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file      = r.Value();

    uint8_t           buffer[4] = {};
    VFSResult<size_t> read      = file->Read(ArrayView<uint8_t>(buffer, 4), 3);
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 4u);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer), 4), "3456");

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, Size)
{
    VFSDiskContext ctx(m_root.c_str());
    WriteFile(ctx, "/s.txt", "12345");

    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/s.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*           file = r.Value();

    VFSResult<uint64_t> size = file->Size();
    ASSERT_TRUE(size.Succeeded());
    EXPECT_EQ(size.Value(), 5u);

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, StatReportsSize)
{
    VFSDiskContext ctx(m_root.c_str());
    WriteFile(ctx, "/s.txt", "12345");

    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/s.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*              file = r.Value();

    VFSResult<VFSFileStat> stat = file->Stat();
    ASSERT_TRUE(stat.Succeeded());
    EXPECT_EQ(stat.Value().SizeBytes, 5u);
    EXPECT_FALSE(stat.Value().IsDirectory);

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, ReadAllReadsWholeFile)
{
    VFSDiskContext    ctx(m_root.c_str());
    const std::string contents = "abcdefgh";
    WriteFile(ctx, "/all.txt", contents);

    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/all.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*            file = r.Value();

    std::vector<uint8_t> buffer(contents.size());
    VFSResult<size_t>    read = file->ReadAll(ArrayView<uint8_t>(buffer.data(), buffer.size()));
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), contents.size());
    EXPECT_EQ(std::string(buffer.begin(), buffer.end()), contents);

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, ReadAllStopsAtEofWithLargerBuffer)
{
    VFSDiskContext ctx(m_root.c_str());
    WriteFile(ctx, "/small.txt", "abc");

    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/small.txt").Value(), VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*            file = r.Value();

    std::vector<uint8_t> buffer(100, 0xFF);
    VFSResult<size_t>    read = file->ReadAll(ArrayView<uint8_t>(buffer.data(), buffer.size()));
    ASSERT_TRUE(read.Succeeded());
    EXPECT_EQ(read.Value(), 3u); // stops at EOF, not buffer size
    EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer.data()), 3), "abc");

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, PathReturnsOpenedPath)
{
    VFSDiskContext ctx(m_root.c_str());
    WriteFile(ctx, "/x.txt", "y");

    VFSPath              path = VFSPath::Parse("/x.txt").Value();
    VFSResult<IVFSFile*> r    = ctx.Open(path, VFSOpenFlags::Read);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile* file = r.Value();

    EXPECT_TRUE(file->Path() == path);

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, FlushSucceeds)
{
    VFSDiskContext       ctx(m_root.c_str());
    VFSResult<IVFSFile*> r = ctx.Open(VFSPath::Parse("/f.txt").Value(), VFSOpenFlags::Write);
    ASSERT_TRUE(r.Succeeded());
    IVFSFile*         file      = r.Value();

    const char        payload[] = "data";
    VFSResult<size_t> wrote     = file->Write(ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(payload), 4), 0);
    ASSERT_TRUE(wrote.Succeeded());
    EXPECT_TRUE(file->Flush().Succeeded());

    ctx.Close(file);
}

TEST_F(VFSDiskFileTest, ContextExists)
{
    VFSDiskContext ctx(m_root.c_str());
    VFSPath        path = VFSPath::Parse("/e.txt").Value();

    EXPECT_FALSE(ctx.Exists(path));
    WriteFile(ctx, "/e.txt", "hi");
    EXPECT_TRUE(ctx.Exists(path));
}

TEST_F(VFSDiskFileTest, ContextListReturnsEntries)
{
    VFSDiskContext ctx(m_root.c_str());
    WriteFile(ctx, "/a.txt", "a");
    WriteFile(ctx, "/b.txt", "bb");

    MemoryManager manager{};
    manager.Initialize({.BufferSize = ZKilo(64)});

    VFSResult<ZEngine::Core::Containers::Array<VFSDirEntry>> result = ctx.List(&manager.MainArena, VFSPath::Root());
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Value().size(), 2u);

    bool found_a = false;
    bool found_b = false;
    for (const VFSDirEntry& entry : result.Value())
    {
        if (entry.Path.Filename().Equals("a.txt"))
        {
            found_a = true;
        }
        if (entry.Path.Filename().Equals("b.txt"))
        {
            found_b = true;
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);

    manager.Shutdown();
}
