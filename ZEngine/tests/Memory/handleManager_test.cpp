#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/HandleManager.h>
#include <gtest/gtest.h>
#include <thread>

class HandleManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manager.Initialize({.BufferSize = ZKilo(10)});
        auto arena = &(manager.MainArena);
        handle_manager.Initialize(arena, 10);
    }

    void TearDown() override
    {
        manager.Shutdown();
    }

    MemoryManager                         manager{};
    ZEngine::Helpers::HandleManager<int*> handle_manager;
};

TEST_F(HandleManagerTest, CreateHandle)
{
    auto handle = handle_manager.Create();
    EXPECT_TRUE(handle.Valid());
    EXPECT_GE(handle.Index, 0);
    EXPECT_LT(handle.Index, handle_manager.Size());
}

TEST_F(HandleManagerTest, AddValue)
{
    int  value  = 42;
    auto handle = handle_manager.Add(&value);

    EXPECT_TRUE(handle.Valid());
    EXPECT_EQ(*handle_manager[handle], 42);
}

TEST_F(HandleManagerTest, UpdateValue)
{
    int  value1 = 42;
    int  value2 = 84;

    auto handle = handle_manager.Add(&value1);
    EXPECT_EQ(*handle_manager[handle], 42);

    handle_manager.Update(handle, &value2);
    EXPECT_EQ(*handle_manager[handle], 84);
}

TEST_F(HandleManagerTest, MultipleUpdates)
{
    int  value1 = 10;
    int  value2 = 20;
    int  value3 = 30;

    auto handle = handle_manager.Add(&value1);
    EXPECT_EQ(*handle_manager[handle], 10);

    handle_manager.Update(handle, &value2);
    EXPECT_EQ(*handle_manager[handle], 20);

    handle_manager.Update(handle, &value3);
    EXPECT_EQ(*handle_manager[handle], 30);
}

TEST_F(HandleManagerTest, RemoveHandle)
{
    int  value  = 42;
    auto handle = handle_manager.Add(&value);
    EXPECT_TRUE(handle.Valid());

    handle_manager.Remove(handle);
    EXPECT_FALSE(handle.Valid());
}

TEST_F(HandleManagerTest, FullCapacity)
{
    std::vector<ZEngine::Helpers::Handle<int*>> handles;
    std::vector<int>                            values(handle_manager.Capacity());

    for (size_t i = 0; i < handle_manager.Capacity(); ++i)
    {
        values[i]   = static_cast<int>(i);
        auto handle = handle_manager.Add(&values[i]);
        EXPECT_TRUE(handle.Valid());
        handles.push_back(handle);
    }

    int  extraValue    = 999;
    auto invalidHandle = handle_manager.Add(&extraValue);
    EXPECT_FALSE(invalidHandle.Valid());
}

TEST_F(HandleManagerTest, ReuseSlot)
{
    int  value1     = 42;
    int  value2     = 84;

    auto handle1    = handle_manager.Add(&value1);
    int  firstIndex = handle1.Index;
    handle_manager.Remove(handle1);

    auto handle2 = handle_manager.Add(&value2);
    EXPECT_EQ(handle2.Index, firstIndex);
    EXPECT_TRUE(handle2.Valid());
    EXPECT_EQ(*handle_manager[handle2], 84);
}

TEST_F(HandleManagerTest, ConcurrentAccess)
{
    MemoryManager concurrent_manager{};
    concurrent_manager.Initialize({.BufferSize = ZKilo(64)});
    ZEngine::Helpers::HandleManager<int*> h_manager;
    h_manager.Initialize(&(concurrent_manager.MainArena), 40);
    const int                numThreads             = 4;
    const int                numOperationsPerThread = 10;
    std::vector<std::thread> threads;
    std::mutex               printMutex;

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, i, numOperationsPerThread, &printMutex, &h_manager]() {
            std::vector<ZEngine::Helpers::Handle<int*>> handles;
            for (int j = 0; j < numOperationsPerThread; ++j)
            {
                int* value  = new int(i * numOperationsPerThread + j);
                auto handle = h_manager.Add(value);

                EXPECT_EQ(*h_manager[handle], i * numOperationsPerThread + j);
                handles.push_back(handle);
            }

            for (auto& handle : handles)
            {
                h_manager.Remove(handle);
                EXPECT_FALSE(handle.Valid());
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }
    concurrent_manager.Shutdown();
}
