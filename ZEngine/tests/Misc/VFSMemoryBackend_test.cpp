#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <atomic>
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
