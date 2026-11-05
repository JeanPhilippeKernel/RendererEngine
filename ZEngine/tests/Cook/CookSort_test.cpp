#include <ZEngine/Cook/CookSort.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <gtest/gtest.h>
#include <algorithm>

namespace ZEngine::Cook
{
    namespace
    {
        struct MoveOnlySortRecord
        {
            uint32_t Key                                             = UINT32_MAX;
            uint32_t Cookie                                          = UINT32_MAX;

            MoveOnlySortRecord()                                     = default;
            MoveOnlySortRecord(const MoveOnlySortRecord&)            = delete;
            MoveOnlySortRecord& operator=(const MoveOnlySortRecord&) = delete;
            MoveOnlySortRecord& operator=(MoveOnlySortRecord&& other) noexcept
            {
                Key          = other.Key;
                Cookie       = other.Cookie;
                other.Key    = UINT32_MAX;
                other.Cookie = UINT32_MAX;
                return *this;
            }
        };
    } // namespace

    TEST(CookSortTest, EveryPermutationThroughEightElementsMatchesCanonicalOrder)
    {
        Core::Containers::FixedArray<uint32_t, 8> input  = {};
        Core::Containers::FixedArray<uint32_t, 8> actual = {};
        Detail::CookSortState<uint32_t>           state  = {};
        for (size_t count = 0; count <= input.size(); ++count)
        {
            for (size_t i = 0; i < count; ++i)
                input[i] = static_cast<uint32_t>(i);
            do
            {
                actual = input;
                Core::Containers::HeapSort<uint32_t>({actual.data(), count}, state, [](uint32_t a, uint32_t b) { return a < b; });
                for (size_t i = 0; i < count; ++i)
                    ASSERT_EQ(actual[i], i) << "count=" << count;
            } while (std::next_permutation(input.data(), input.data() + count));
        }
    }

    TEST(CookSortTest, LargeTablesDuplicatesAndReverseOrderingStayWithinNLogNComparisons)
    {
        constexpr size_t                                     capacity  = 8193;
        Core::Containers::FixedArray<uint32_t, capacity + 2> actual    = {};
        Core::Containers::FixedArray<uint32_t, capacity>     reference = {};
        Detail::CookSortState<uint32_t>                      state     = {};
        for (const size_t count : {size_t{0}, size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{7}, size_t{8}, size_t{9}, size_t{127}, size_t{128}, size_t{129}, capacity - 1, capacity})
            for (uint32_t pattern = 0; pattern < 5; ++pattern)
                for (const bool descending : {false, true})
                {
                    actual[0]         = UINT32_MAX;
                    actual[count + 1] = UINT32_MAX - 1;
                    uint32_t random   = 0x12345678;
                    for (size_t i = 0; i < count; ++i)
                    {
                        random               ^= random << 13;
                        random               ^= random >> 17;
                        random               ^= random << 5;
                        const uint32_t value  = pattern == 0 ? static_cast<uint32_t>(i) : pattern == 1 ? static_cast<uint32_t>(count - i) : pattern == 2 ? 7 : pattern == 3 ? static_cast<uint32_t>(i % 13) : random;
                        actual[i + 1]         = value;
                        reference[i]          = value;
                    }
                    const auto before = [descending](uint32_t a, uint32_t b) { return descending ? a > b : a < b; };
                    std::sort(reference.data(), reference.data() + count, before);
                    uint64_t comparisons = 0;
                    Core::Containers::HeapSort<uint32_t>({actual.data() + 1, count}, state, [&](uint32_t a, uint32_t b) {
                        ++comparisons;
                        return before(a, b);
                    });
                    for (size_t i = 0; i < count; ++i)
                        ASSERT_EQ(actual[i + 1], reference[i]) << "count=" << count << " pattern=" << pattern << " descending=" << descending;
                    EXPECT_EQ(actual[0], UINT32_MAX);
                    EXPECT_EQ(actual[count + 1], UINT32_MAX - 1);
                    uint64_t levels = 0;
                    for (size_t n = count; n > 1; n = n / 2 + n % 2)
                        ++levels;
                    EXPECT_LE(comparisons, 2 * count * (levels + 1));
                    if (count < 2)
                        EXPECT_EQ(comparisons, 0u);
                }
    }

    TEST(CookSortTest, HeapPushAndPopOrderEveryPermutationThroughSevenElements)
    {
        Core::Containers::FixedArray<uint32_t, 7> input = {};
        Core::Containers::FixedArray<uint32_t, 7> heap  = {};
        Detail::CookSortState<uint32_t>           state = {};
        for (size_t count = 0; count <= input.size(); ++count)
        {
            for (size_t i = 0; i < count; ++i)
                input[i] = static_cast<uint32_t>(i);
            do
            {
                for (const bool minimumFirst : {false, true})
                {
                    const auto before = [minimumFirst](uint32_t a, uint32_t b) { return minimumFirst ? a > b : a < b; };
                    Core::Containers::HeapPush<uint32_t>({}, state, before);
                    Core::Containers::HeapPop<uint32_t>({}, state, before);
                    for (size_t i = 0; i < count; ++i)
                    {
                        heap[i] = input[i];
                        Core::Containers::HeapPush<uint32_t>({heap.data(), i + 1}, state, before);
                        ASSERT_TRUE(std::is_heap(heap.data(), heap.data() + i + 1, before));
                    }
                    for (size_t remaining = count; remaining; --remaining)
                    {
                        Core::Containers::HeapPop<uint32_t>({heap.data(), remaining}, state, before);
                        EXPECT_EQ(heap[remaining - 1], minimumFirst ? count - remaining : remaining - 1);
                        ASSERT_TRUE(std::is_heap(heap.data(), heap.data() + remaining - 1, before));
                    }
                }
            } while (std::next_permutation(input.data(), input.data() + count));
        }
    }

    TEST(CookSortTest, LargeInterleavedQueuesMatchStandardHeapWithLogarithmicOperations)
    {
        constexpr size_t                                     capacity  = 8193;
        Core::Containers::FixedArray<uint32_t, capacity + 2> values    = {};
        Core::Containers::FixedArray<uint32_t, capacity>     reference = {};
        Detail::CookSortState<uint32_t>                      state     = {};
        for (uint32_t pattern = 0; pattern < 5; ++pattern)
            for (const bool minimumFirst : {false, true})
                for (const bool interleaved : {false, true})
                {
                    values[0]              = UINT32_MAX;
                    values[capacity + 1]   = UINT32_MAX - 1;
                    size_t     count       = 0;
                    uint32_t   random      = 0x12345678;
                    uint64_t   comparisons = 0;
                    const auto before      = [minimumFirst](uint32_t a, uint32_t b) { return minimumFirst ? a > b : a < b; };
                    const auto counted     = [&](uint32_t a, uint32_t b) {
                        ++comparisons;
                        return before(a, b);
                    };
                    const auto bound = [&] {
                        uint64_t levels = 0;
                        for (size_t n = count; n > 1; n = n / 2 + n % 2)
                            ++levels;
                        return 2 * (levels + 1);
                    };
                    const auto pop = [&] {
                        const auto previous = comparisons;
                        Core::Containers::HeapPop<uint32_t>({values.data() + 1, count}, state, counted);
                        EXPECT_LE(comparisons - previous, bound());
                        std::pop_heap(reference.data(), reference.data() + count, before);
                        --count;
                        ASSERT_EQ(values[count + 1], reference[count]);
                        if (count)
                            ASSERT_EQ(values[1], reference[0]);
                    };
                    for (size_t i = 0; i < capacity; ++i)
                    {
                        random               ^= random << 13;
                        random               ^= random >> 17;
                        random               ^= random << 5;
                        const uint32_t value  = pattern == 0 ? static_cast<uint32_t>(i) : pattern == 1 ? static_cast<uint32_t>(capacity - i) : pattern == 2 ? 7 : pattern == 3 ? static_cast<uint32_t>(i % 13) : random;
                        values[count + 1] = reference[count] = value;
                        ++count;
                        const auto previous = comparisons;
                        Core::Containers::HeapPush<uint32_t>({values.data() + 1, count}, state, counted);
                        EXPECT_LE(comparisons - previous, bound());
                        std::push_heap(reference.data(), reference.data() + count, before);
                        ASSERT_EQ(values[1], reference[0]);
                        if (interleaved && i % 3 == 0)
                            pop();
                    }
                    while (count)
                        pop();
                    EXPECT_EQ(values[0], UINT32_MAX);
                    EXPECT_EQ(values[capacity + 1], UINT32_MAX - 1);
                }
    }

    TEST(CookSortTest, BottomUpHeapConstructionHasLinearComparisonsAndReusesSortingState)
    {
        constexpr size_t                                     capacity = 8193;
        Core::Containers::FixedArray<uint32_t, capacity + 2> values   = {};
        Detail::CookSortState<uint32_t>                      state    = {};
        for (const size_t count : {size_t{0}, size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{127}, size_t{128}, size_t{129}, capacity - 1, capacity})
            for (uint32_t pattern = 0; pattern < 5; ++pattern)
                for (const bool minimumFirst : {false, true})
                {
                    values[0]         = UINT32_MAX;
                    values[count + 1] = UINT32_MAX - 1;
                    uint32_t random   = 0x12345678;
                    for (size_t i = 0; i < count; ++i)
                    {
                        random        ^= random << 13;
                        random        ^= random >> 17;
                        random        ^= random << 5;
                        values[i + 1]  = pattern == 0 ? static_cast<uint32_t>(i) : pattern == 1 ? static_cast<uint32_t>(count - i) : pattern == 2 ? 7 : pattern == 3 ? static_cast<uint32_t>(i % 13) : random;
                    }
                    uint64_t   comparisons = 0;
                    const auto before      = [minimumFirst](uint32_t a, uint32_t b) { return minimumFirst ? a > b : a < b; };
                    Core::Containers::HeapMake<uint32_t>({values.data() + 1, count}, state, [&](uint32_t a, uint32_t b) {
                        ++comparisons;
                        return before(a, b);
                    });
                    EXPECT_LE(comparisons, 2 * count);
                    EXPECT_EQ(state.HeapCount, count);
                    EXPECT_EQ(state.Start, 0u);
                    ASSERT_TRUE(std::is_heap(values.data() + 1, values.data() + count + 1, before));
                    Core::Containers::HeapSort<uint32_t>({values.data() + 1, count}, state, before);
                    ASSERT_TRUE(std::is_sorted(values.data() + 1, values.data() + count + 1, before));
                    EXPECT_EQ(values[0], UINT32_MAX);
                    EXPECT_EQ(values[count + 1], UINT32_MAX - 1);
                }
    }

    TEST(CookSortTest, HeapQueuePreservesEveryOwnedRecordAcrossPushAndPop)
    {
        Core::Containers::FixedArray<MoveOnlySortRecord, 257> values = {};
        Detail::CookSortState<MoveOnlySortRecord>             state  = {};
        for (const bool minimumFirst : {false, true})
        {
            const auto before = [minimumFirst](const auto& a, const auto& b) { return minimumFirst ? a.Key > b.Key : a.Key < b.Key; };
            for (uint32_t i = 0; i < values.size(); ++i)
            {
                values[i].Key    = (i * 71) % static_cast<uint32_t>(values.size());
                values[i].Cookie = values[i].Key ^ 0xabcd1234;
                Core::Containers::HeapPush<MoveOnlySortRecord>({values.data(), i + 1}, state, before);
            }
            for (size_t remaining = values.size(); remaining; --remaining)
            {
                Core::Containers::HeapPop<MoveOnlySortRecord>({values.data(), remaining}, state, before);
                const uint32_t expected = static_cast<uint32_t>(minimumFirst ? values.size() - remaining : remaining - 1);
                EXPECT_EQ(values[remaining - 1].Key, expected);
                EXPECT_EQ(values[remaining - 1].Cookie, expected ^ 0xabcd1234);
            }
            EXPECT_EQ(state.Value.Key, UINT32_MAX);
            EXPECT_EQ(state.Value.Cookie, UINT32_MAX);
        }
    }

    TEST(CookSortTest, MoveOnlyRecordsKeepEveryOwnedValueAndReuseTheSameState)
    {
        Core::Containers::FixedArray<MoveOnlySortRecord, 257> values = {};
        Detail::CookSortState<MoveOnlySortRecord>             state  = {};
        for (uint32_t i = 0; i < values.size(); ++i)
        {
            values[i].Key    = (i * 71) % static_cast<uint32_t>(values.size());
            values[i].Cookie = values[i].Key ^ 0xabcd1234;
        }
        for (const bool descending : {false, true, false})
        {
            Core::Containers::HeapSort<MoveOnlySortRecord>(values, state, [descending](const auto& a, const auto& b) { return descending ? a.Key > b.Key : a.Key < b.Key; });
            for (size_t i = 0; i < values.size(); ++i)
            {
                const uint32_t expected = static_cast<uint32_t>(descending ? values.size() - i - 1 : i);
                EXPECT_EQ(values[i].Key, expected);
                EXPECT_EQ(values[i].Cookie, expected ^ 0xabcd1234);
            }
            EXPECT_EQ(state.Value.Key, UINT32_MAX);
            EXPECT_EQ(state.Value.Cookie, UINT32_MAX);
        }
    }
} // namespace ZEngine::Cook
