#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <gtest/gtest.h>
#include <utility>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;

static_assert(std::is_trivially_copyable_v<ArrayView<int>>);
static_assert(std::is_constructible_v<ArrayView<const int>, ArrayView<int>>);
static_assert(!std::is_constructible_v<ArrayView<int>, ArrayView<const int>>);
static_assert(!std::is_constructible_v<ArrayView<const int>, FixedArray<int, 2>&&>);

TEST(ArrayViewTest, EmptyConstAndByteViewsPreserveBorrowedStorage)
{
    const ArrayView<int> empty = {};
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(empty.begin(), empty.end());
    EXPECT_EQ(empty.first(0).data(), nullptr);
    EXPECT_EQ(empty.subspan(0).data(), nullptr);
    int                  values[3] = {4, 5, 6};
    const ArrayView<int> view(values);
    view[0] = 9; // Constness belongs to the element, not the view wrapper.
    const ArrayView<const int> readonly(view);
    EXPECT_EQ(readonly.front(), 9);
    EXPECT_EQ(readonly.back(), 6);
    EXPECT_EQ(readonly.first(2).size(), 2u);
    EXPECT_EQ(readonly.subspan(1).front(), 5);
    EXPECT_TRUE(readonly.subspan(3).empty());
    EXPECT_EQ(readonly.subspan(3).data(), values + 3);
    EXPECT_EQ(AsBytes(readonly).size(), sizeof(values));
    EXPECT_EQ(AsBytes(readonly).data(), reinterpret_cast<const std::byte*>(values));
    FixedArray<int, 2> inline_values{1, 2};
    const auto         deduced = ArrayView(inline_values);
    static_assert(std::is_same_v<decltype(deduced), const ArrayView<int>>);
    EXPECT_EQ(deduced.back(), 2);
}

TEST(FixedArrayTest, DefaultInitializationAndNormalElementLifetimes)
{
    FixedArray<int, 3> zero = {};
    for (const auto value : zero)
        EXPECT_EQ(value, 0);
    constexpr FixedArray values{1, 2, 3};
    static_assert(values.size() == 3 && values.Values[2] == 3);
    EXPECT_EQ(zero.size(), 3u);
    EXPECT_FALSE(zero == values);
    struct Element
    {
        int* Destroyed = nullptr;
        ~Element()
        {
            if (Destroyed)
                ++*Destroyed;
        }
    };
    int destroyed = 0;
    {
        FixedArray<Element, 2> owned = {};
        for (auto& element : owned)
            element.Destroyed = &destroyed;
    }
    EXPECT_EQ(destroyed, 2);
}

TEST(ArrayCheckedAppendTest, GrowthCopiesAliasedInputAndFailurePreservesExistingRecords)
{
    ArenaAllocator arena;
    arena.Initialize(4096, 0, "ArrayCheckedAppendTest");
    ASSERT_NE(arena.m_memory, nullptr);
    Array<uint32_t> array;
    array.m_allocator = &arena;
    ASSERT_TRUE(array.try_reserve(1));
    ASSERT_TRUE(array.try_push(42));
    ASSERT_TRUE(array.try_push(array[0]));
    EXPECT_EQ(array[0], 42u);
    EXPECT_EQ(array[1], 42u);
    while (array.size() < array.capacity())
        ASSERT_TRUE(array.try_push(7));
    ASSERT_NE(arena.TryResize(nullptr, 0, arena.m_total_size - arena.m_current_offset, 1), nullptr);
    const auto* address  = array.data();
    const auto  count    = array.size();
    const auto  capacity = array.capacity();
    EXPECT_FALSE(array.try_push(array[0]));
    EXPECT_EQ(array.data(), address);
    EXPECT_EQ(array.size(), count);
    EXPECT_EQ(array.capacity(), capacity);
    EXPECT_EQ(array[0], 42u);
}

class ArrayTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manager.Initialize(200, {});
    }

    void TearDown() override
    {
        manager.Shutdown();
    }
    MemoryManager manager;
};

TEST_F(ArrayTest, InitialState)
{
    Array<int> array;
    array.init(&manager.MainArena, 10);

    EXPECT_EQ(array.size(), 0);
    EXPECT_EQ(array.capacity(), 10);
    EXPECT_TRUE(array.empty());
}

TEST_F(ArrayTest, PushBack)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    EXPECT_EQ(array.size(), 3);
    EXPECT_FALSE(array.empty());
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);
    EXPECT_EQ(array[2], 3);
}

TEST_F(ArrayTest, AutoResize)
{
    Array<int> array;
    array.init(&manager.MainArena, 2);

    EXPECT_EQ(array.capacity(), 2);

    array.push(1);
    array.push(2);
    array.push(3);

    EXPECT_EQ(array.size(), 3);
    EXPECT_EQ(array.capacity(), 4);
    EXPECT_EQ(array[2], 3);
}

TEST_F(ArrayTest, PopBack)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    EXPECT_EQ(array.size(), 3);

    array.pop();
    EXPECT_EQ(array.size(), 2);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);

    array.pop();
    array.pop();
    EXPECT_EQ(array.size(), 0);
    EXPECT_TRUE(array.empty());
}

TEST_F(ArrayTest, Clear)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    EXPECT_EQ(array.size(), 3);

    array.clear();

    EXPECT_EQ(array.size(), 0);
    EXPECT_TRUE(array.empty());
    EXPECT_EQ(array.capacity(), 4);
}

TEST_F(ArrayTest, Reserve)
{
    Array<int> array;
    array.init(&manager.MainArena, 4);

    EXPECT_EQ(array.capacity(), 4);

    array.reserve(8);
    EXPECT_EQ(array.capacity(), 8);

    array.reserve(6);
    EXPECT_EQ(array.capacity(), 8);
}

TEST_F(ArrayTest, CheckedReservePreservesValues)
{
    Array<int> array = {};
    array.init(&manager.MainArena, 2);
    array.push(17);
    array.push(29);
    EXPECT_TRUE(array.try_reserve(16));
    EXPECT_EQ(array.capacity(), 16u);
    EXPECT_EQ(array.size(), 2u);
    EXPECT_EQ(array[0], 17);
    EXPECT_EQ(array[1], 29);
    EXPECT_TRUE(array.try_reserve(4));
    EXPECT_EQ(array.capacity(), 16u);
}

TEST_F(ArrayTest, CheckedReserveRejectsOverflowWithoutChangingStorage)
{
    Array<int> array = {};
    array.init(&manager.MainArena, 2);
    array.push(41);
    const auto* storage = array.data();
    EXPECT_FALSE(array.try_reserve(SIZE_MAX));
    EXPECT_EQ(array.data(), storage);
    EXPECT_EQ(array.capacity(), 2u);
    EXPECT_EQ(array.size(), 1u);
    EXPECT_EQ(array[0], 41);
}

TEST_F(ArrayTest, CheckedReserveRejectsExhaustedArenaWithoutChangingStorage)
{
    Array<int> array = {};
    array.init(&manager.MainArena, 2);
    array.push(53);
    const auto* storage = array.data();
    EXPECT_FALSE(array.try_reserve(manager.MainArena.m_total_size));
    EXPECT_EQ(array.data(), storage);
    EXPECT_EQ(array.capacity(), 2u);
    EXPECT_EQ(array.size(), 1u);
    EXPECT_EQ(array[0], 53);
}

TEST_F(ArrayTest, CheckedReserveRejectsMissingAllocator)
{
    Array<int> array = {};
    EXPECT_TRUE(array.try_reserve(0));
    EXPECT_FALSE(array.try_reserve(4));
    EXPECT_EQ(array.data(), nullptr);
    EXPECT_EQ(array.capacity(), 0u);
}

TEST_F(ArrayTest, CheckedReservePreservesSlabStorageOnExhaustion)
{
    ArenaAllocator slab_arena = {};
    slab_arena.Initialize(64 * 1024, 4096);
    TLSFSlab slab = {};
    slab.Init(&slab_arena, 64 * 1024);
    {
        Array<int> array = {};
        array.init(&slab, 2);
        array.push(67);
        const auto* storage = array.data();
        EXPECT_FALSE(array.try_reserve(64 * 1024));
        EXPECT_EQ(array.data(), storage);
        EXPECT_EQ(array.capacity(), 2u);
        EXPECT_EQ(array.size(), 1u);
        EXPECT_EQ(array[0], 67);
        EXPECT_TRUE(array.try_reserve(16));
        EXPECT_EQ(array[0], 67);
    }
    slab.Shutdown();
}

TEST_F(ArrayTest, FrontAndBack)
{
    Array<int> array;
    array.init(&manager.MainArena, 4);

    array.push(10);
    EXPECT_EQ(array.front(), 10);
    EXPECT_EQ(array.back(), 10);

    array.push(20);
    array.push(30);

    EXPECT_EQ(array.front(), 10);
    EXPECT_EQ(array.back(), 30);
}

TEST_F(ArrayTest, ArrayViewWrap)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 10, 20, 30));

    ArrayView<int> view(array);

    EXPECT_EQ(view.size(), array.size());
    EXPECT_EQ(view[0], 10);
    EXPECT_EQ(view[1], 20);
    EXPECT_EQ(view[2], 30);

    view[1] = 99;
    EXPECT_EQ(array[1], 99);
}

TEST_F(ArrayTest, MoveConstructorTransfersBufferAndEmptiesSource)
{
    Array<int> src;
    src.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    const int*   src_data = src.data();
    const size_t src_cap  = src.capacity();

    Array<int>   dst      = std::move(src);

    EXPECT_EQ(dst.data(), src_data);
    EXPECT_EQ(dst.size(), 3u);
    EXPECT_EQ(dst.capacity(), src_cap);
    EXPECT_EQ(dst[0], 1);
    EXPECT_EQ(dst[1], 2);
    EXPECT_EQ(dst[2], 3);

    EXPECT_EQ(src.size(), 0u);
    EXPECT_EQ(src.capacity(), 0u);
    EXPECT_EQ(src.data(), nullptr);
}

TEST_F(ArrayTest, MoveAssignmentTransfersBufferAndEmptiesSource)
{
    Array<int> src;
    src.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 5, 6, 7));
    const int* src_data = src.data();

    Array<int> dst;
    dst.init(&manager.MainArena, 2);
    dst.push(99);

    dst = std::move(src);

    EXPECT_EQ(dst.data(), src_data);
    EXPECT_EQ(dst.size(), 3u);
    EXPECT_EQ(dst[0], 5);
    EXPECT_EQ(dst[2], 7);

    EXPECT_EQ(src.size(), 0u);
    EXPECT_EQ(src.capacity(), 0u);
    EXPECT_EQ(src.data(), nullptr);
}

TEST_F(ArrayTest, MoveAssignmentSelfIsSafe)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    const int*  data  = array.data();

    // Guarded by (this != &other); aliased through a reference to dodge -Wself-move.
    Array<int>& alias = array;
    array             = std::move(alias);

    EXPECT_EQ(array.size(), 3u);
    EXPECT_EQ(array.data(), data);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[2], 3);
}

TEST_F(ArrayTest, MovedFromArrayIsReusable)
{
    Array<int> src;
    src.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    Array<int> dst = std::move(src);

    // The moved-from array is empty but valid — re-init and use it again.
    src.init(&manager.MainArena, 2);
    src.push(42);

    EXPECT_EQ(src.size(), 1u);
    EXPECT_EQ(src[0], 42);
    EXPECT_EQ(dst.size(), 3u); // dst unaffected by src's reuse
}

TEST_F(ArrayTest, InsertAtBeginning)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 2, 3, 4));

    array.insert(0, 1);

    EXPECT_EQ(array.size(), 4u);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);
    EXPECT_EQ(array[2], 3);
    EXPECT_EQ(array[3], 4);
}

TEST_F(ArrayTest, InsertInMiddle)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 4));

    array.insert(2, 3);

    EXPECT_EQ(array.size(), 4u);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);
    EXPECT_EQ(array[2], 3);
    EXPECT_EQ(array[3], 4);
}

TEST_F(ArrayTest, InsertAtEnd)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    array.insert(array.size(), 4);

    EXPECT_EQ(array.size(), 4u);
    EXPECT_EQ(array[3], 4);
}

TEST_F(ArrayTest, InsertGrowsWhenFull)
{
    Array<int> array;
    array.init(&manager.MainArena, 2);
    array.push(1);
    array.push(3);

    array.insert(1, 2);

    EXPECT_EQ(array.size(), 3u);
    EXPECT_GE(array.capacity(), 3u);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);
    EXPECT_EQ(array[2], 3);
}

TEST_F(ArrayTest, EraseFromMiddle)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3, 4));

    array.erase(1);

    EXPECT_EQ(array.size(), 3u);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 3);
    EXPECT_EQ(array[2], 4);
}

TEST_F(ArrayTest, EraseFirstElement)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    array.erase(0);

    EXPECT_EQ(array.size(), 2u);
    EXPECT_EQ(array[0], 2);
    EXPECT_EQ(array[1], 3);
}

TEST_F(ArrayTest, EraseLastElement)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2, 3));

    array.erase(array.size() - 1);

    EXPECT_EQ(array.size(), 2u);
    EXPECT_EQ(array[0], 1);
    EXPECT_EQ(array[1], 2);
}

TEST_F(ArrayTest, EraseUntilEmpty)
{
    Array<int> array;
    array.init(&manager.MainArena, 4, make_initializer_list(&manager.MainArena, 1, 2));

    array.erase(0);
    array.erase(0);

    EXPECT_EQ(array.size(), 0u);
    EXPECT_TRUE(array.empty());
}
