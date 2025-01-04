#include <Helpers/HandleManager.h>
#include <gtest/gtest.h>
#include <thread>

class HandleManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manager = std::make_unique<ZEngine::Helpers::HandleManager<int*>>(10);
    }

    void TearDown() override
    {
        manager.reset();
    }

    std::unique_ptr<ZEngine::Helpers::HandleManager<int*>> manager;
};

TEST_F(HandleManagerTest, CreateHandle)
{
    auto handle = manager->Create();
    EXPECT_TRUE(handle.Valid());
    EXPECT_GE(handle.Index, 0);
    EXPECT_LT(handle.Index, manager->Size());
}

TEST_F(HandleManagerTest, AddValue)
{
    int  value  = 42;
    auto handle = manager->Add(&value);

    EXPECT_TRUE(handle.Valid());
    EXPECT_EQ(*(*manager)[handle], 42);
}

TEST_F(HandleManagerTest, UpdateValue)
{
    int value1 = 42;
    int value2 = 84;

    auto handle = manager->Add(&value1);
    EXPECT_EQ(*(*manager)[handle], 42);

    manager->Update(handle, &value2);
    EXPECT_EQ(*(*manager)[handle], 84);
}

TEST_F(HandleManagerTest, MultipleUpdates)
{
    int value1 = 10;
    int value2 = 20;
    int value3 = 30;

    auto handle = manager->Add(&value1);
    EXPECT_EQ(*(*manager)[handle], 10);

    manager->Update(handle, &value2);
    EXPECT_EQ(*(*manager)[handle], 20);

    manager->Update(handle, &value3);
    EXPECT_EQ(*(*manager)[handle], 30);
}

TEST_F(HandleManagerTest, RemoveHandle)
{
    int  value  = 42;
    auto handle = manager->Add(&value);
    EXPECT_TRUE(handle.Valid());

    manager->Remove(handle);
    EXPECT_FALSE(handle.Valid());
}

TEST_F(HandleManagerTest, FullCapacity)
{
    std::vector<ZEngine::Helpers::Handle<int*>> handles;
    std::vector<int>                            values(manager->Size());

    for (size_t i = 0; i < manager->Size(); ++i)
    {
        values[i]   = static_cast<int>(i);
        auto handle = manager->Add(&values[i]);
        EXPECT_TRUE(handle.Valid());
        handles.push_back(handle);
    }

    int  extraValue    = 999;
    auto invalidHandle = manager->Add(&extraValue);
    EXPECT_FALSE(invalidHandle.Valid());
}

TEST_F(HandleManagerTest, ReuseSlot)
{
    int value1 = 42;
    int value2 = 84;

    auto handle1    = manager->Add(&value1);
    int  firstIndex = handle1.Index;
    manager->Remove(handle1);

    auto handle2 = manager->Add(&value2);
    EXPECT_EQ(handle2.Index, firstIndex);
    EXPECT_TRUE(handle2.Valid());
    EXPECT_EQ(*(*manager)[handle2], 84);
}

TEST_F(HandleManagerTest, ConcurrentAccess)
{
    manager                                         = std::make_unique<ZEngine::Helpers::HandleManager<int*>>(40);
    const int                numThreads             = 4;
    const int                numOperationsPerThread = 10;
    std::vector<std::thread> threads;
    std::mutex               printMutex;

    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([this, i, numOperationsPerThread, &printMutex]() {
            std::vector<ZEngine::Helpers::Handle<int*>> handles;
            for (int j = 0; j < numOperationsPerThread; ++j)
            {
                int* value  = new int(i * numOperationsPerThread + j);
                auto handle = manager->Add(value);

                EXPECT_EQ(*(*manager)[handle], i * numOperationsPerThread + j);
                handles.push_back(handle);
            }

            for (auto& handle : handles)
            {
                manager->Remove(handle);
                EXPECT_FALSE(handle.Valid());
            }
        });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }
}