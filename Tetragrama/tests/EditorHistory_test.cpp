#include <Tetragrama/EditorHistory.h>
#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <vector>

using namespace Tetragrama;

namespace
{
    struct Change
    {
        int       Before    = 0;
        int       After     = 0;
        uint32_t* Destroyed = nullptr;
    };

    EditorOperation Operation()
    {
        return {
            .Validate =
                [](void* context, std::span<const std::byte> data, bool undo) {
                    if (!context || data.size() < sizeof(Change))
                    {
                        return false;
                    }
                    const auto& change = *reinterpret_cast<const Change*>(data.data());
                    return *static_cast<int*>(context) == (undo ? change.After : change.Before);
                },
            .Apply  = [](void* context, std::span<const std::byte> data) { *static_cast<int*>(context) = reinterpret_cast<const Change*>(data.data())->After; },
            .Revert = [](void* context, std::span<const std::byte> data) { *static_cast<int*>(context) = reinterpret_cast<const Change*>(data.data())->Before; },
            .Destroy =
                [](std::span<std::byte> data) {
                    auto* change = reinterpret_cast<Change*>(data.data());
                    if (change->Destroyed)
                    {
                        ++*change->Destroyed;
                    }
                },
            .Name = [](std::span<const std::byte>) -> cstring { return "Test change"; },
        };
    }

    std::span<const std::byte> Bytes(const Change& change)
    {
        return std::as_bytes(std::span(&change, 1));
    }
} // namespace

class EditorHistoryTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::ArenaAllocator arena   = {};
    EditorHistory                         history = {};
    int                                   value   = 0;

    void                                  SetUp() override
    {
        arena.Initialize(ZMega(1), 4096);
        ASSERT_TRUE(history.Initialize(&arena, 4, 256));
    }

    void TearDown() override
    {
        history.Reset();
    }

    bool Push(int next, uint32_t* destroyed = nullptr)
    {
        return history.ExecuteAndPush(&value, Operation(), Bytes({value, next, destroyed}));
    }
};

TEST_F(EditorHistoryTest, ExecuteUndoRedoAndSavedCheckpointAreExact)
{
    EXPECT_FALSE(history.Dirty());
    ASSERT_TRUE(Push(1));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(history.Dirty());
    EXPECT_STREQ(history.UndoName(), "Test change");
    history.MarkSaved();
    ASSERT_TRUE(Push(2));
    ASSERT_TRUE(history.Undo(&value));
    EXPECT_EQ(value, 1);
    EXPECT_FALSE(history.Dirty());
    ASSERT_TRUE(history.Undo(&value));
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(history.Dirty());
    EXPECT_STREQ(history.RedoName(), "Test change");
    ASSERT_TRUE(history.Redo(&value));
    EXPECT_FALSE(history.Dirty());
    ASSERT_TRUE(history.Redo(&value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(history.Dirty());
    EXPECT_FALSE(history.Redo(&value));
}

TEST_F(EditorHistoryTest, AlreadyAppliedTransactionIsNotAppliedTwice)
{
    const Change change{0, 1};
    value = 1;
    ASSERT_TRUE(history.PushAlreadyApplied(Operation(), Bytes(change)));
    EXPECT_EQ(value, 1);
    ASSERT_TRUE(history.Undo(&value));
    EXPECT_EQ(value, 0);
}

TEST_F(EditorHistoryTest, InvalidOperationAndOversizedPayloadLeaveHistoryAndSceneUntouched)
{
    ASSERT_TRUE(Push(1));
    const auto bytes = history.BytesUsed();
    EXPECT_FALSE(history.ExecuteAndPush(&value, Operation(), Bytes({99, 2})));
    std::array<std::byte, 257> oversized = {};
    EXPECT_FALSE(history.PushAlreadyApplied(Operation(), oversized));
    EXPECT_FALSE(history.PushAlreadyApplied({}, Bytes({1, 2})));
    EXPECT_EQ(value, 1);
    EXPECT_EQ(history.BytesUsed(), bytes);
    EXPECT_EQ(history.UndoCount(), 1u);
}

TEST_F(EditorHistoryTest, FailedUndoDoesNotMoveCursorOrChangeCheckpoint)
{
    ASSERT_TRUE(Push(1));
    value = 99;
    EXPECT_FALSE(history.Undo(&value));
    EXPECT_EQ(value, 99);
    EXPECT_EQ(history.UndoCount(), 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_TRUE(history.Dirty());
}

TEST_F(EditorHistoryTest, RedoBranchReplacementReclaimsPayloadsAndNeverReusesCheckpointIDs)
{
    uint32_t destroyed = 0;
    ASSERT_TRUE(Push(1, &destroyed));
    ASSERT_TRUE(Push(2, &destroyed));
    history.MarkSaved();
    ASSERT_TRUE(history.Undo(&value));
    ASSERT_TRUE(Push(3, &destroyed));
    EXPECT_EQ(destroyed, 1u);
    EXPECT_EQ(history.RedoCount(), 0u);
    EXPECT_TRUE(history.Dirty());
    EXPECT_EQ(history.BytesUsed(), 2 * sizeof(Change));
    history.Reset();
    EXPECT_EQ(destroyed, 3u);
}

TEST_F(EditorHistoryTest, EntryLimitEvictsAndReclaimsOldestEntries)
{
    uint32_t destroyed = 0;
    for (int i = 1; i <= 1000; ++i)
    {
        ASSERT_TRUE(Push(i, &destroyed));
    }
    EXPECT_EQ(history.UndoCount(), 4u);
    EXPECT_EQ(destroyed, 996u);
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE(history.Undo(&value));
    }
    EXPECT_EQ(value, 996);
    EXPECT_FALSE(history.Undo(&value));
    history.Reset();
    EXPECT_EQ(destroyed, 1000u);
    EXPECT_EQ(history.BytesUsed(), 0u);
}

TEST_F(EditorHistoryTest, VariablePayloadByteLimitWrapsAndRetainsAlignedCompleteEntries)
{
    struct alignas(std::max_align_t) PaddedChange
    {
        Change                    Value   = {};
        std::array<std::byte, 64> Padding = {};
    };
    const auto offset = arena.m_current_offset;
    for (int i = 1; i <= 1000; ++i)
    {
        const PaddedChange change{
            {value, i}
        };
        ASSERT_TRUE(history.ExecuteAndPush(&value, Operation(), std::as_bytes(std::span(&change, 1))));
        EXPECT_LE(history.BytesUsed(), history.ByteCapacity());
    }
    EXPECT_EQ(value, 1000);
    EXPECT_EQ(arena.m_current_offset, offset);
    EXPECT_GE(history.UndoCount(), 2u);
    while (history.UndoCount())
    {
        ASSERT_TRUE(history.Undo(&value));
    }
    while (history.RedoCount())
    {
        ASSERT_TRUE(history.Redo(&value));
    }
    EXPECT_EQ(value, 1000);
}

TEST_F(EditorHistoryTest, InvalidationCannotReportSavedUntilSuccessfulSave)
{
    ASSERT_TRUE(Push(1));
    history.Invalidate();
    EXPECT_EQ(history.UndoCount(), 0u);
    EXPECT_TRUE(history.Dirty());
    ASSERT_TRUE(Push(2));
    ASSERT_TRUE(history.Undo(&value));
    EXPECT_TRUE(history.Dirty());
    history.MarkSaved();
    EXPECT_FALSE(history.Dirty());
}

TEST_F(EditorHistoryTest, MixedSizePayloadsKeepSimdAlignmentAcrossWrapReplayAndBranchReplacement)
{
    struct alignas(16) Payload
    {
        std::byte Bytes[64] = {};
    };
    const Payload         payload   = {};
    const EditorOperation operation = {
        .Validate = [](void*, std::span<const std::byte> bytes, bool) { return !bytes.empty() && reinterpret_cast<uintptr_t>(bytes.data()) % alignof(Payload) == 0; },
        .Apply    = [](void*, std::span<const std::byte> bytes) { EXPECT_EQ(reinterpret_cast<uintptr_t>(bytes.data()) % alignof(Payload), 0u); },
        .Revert   = [](void*, std::span<const std::byte> bytes) { EXPECT_EQ(reinterpret_cast<uintptr_t>(bytes.data()) % alignof(Payload), 0u); },
        .Destroy  = [](std::span<std::byte> bytes) { EXPECT_EQ(reinterpret_cast<uintptr_t>(bytes.data()) % alignof(Payload), 0u); },
        .Name     = [](std::span<const std::byte> bytes) -> cstring {
            EXPECT_EQ(reinterpret_cast<uintptr_t>(bytes.data()) % alignof(Payload), 0u);
            return "Aligned payload";
        },
    };
    const auto offset = arena.m_current_offset;
    for (uint32_t i = 0; i < 200; ++i)
    {
        const size_t bytes = 8 * (1 + i % 7);
        ASSERT_TRUE(history.ExecuteAndPush(nullptr, operation, {payload.Bytes, bytes}));
        ASSERT_FALSE(HasFailure());
        EXPECT_STREQ(history.UndoName(), "Aligned payload");
        ASSERT_TRUE(history.Undo(nullptr));
        EXPECT_STREQ(history.RedoName(), "Aligned payload");
        ASSERT_TRUE(history.Redo(nullptr));
        if (i % 4 == 3)
        {
            ASSERT_TRUE(history.Undo(nullptr)); // next push replaces this redo branch
        }
        EXPECT_LE(history.BytesUsed(), history.ByteCapacity());
    }
    while (history.UndoCount())
    {
        ASSERT_TRUE(history.Undo(nullptr));
    }
    while (history.RedoCount())
    {
        ASSERT_TRUE(history.Redo(nullptr));
    }
    history.Reset();
    EXPECT_EQ(history.BytesUsed(), 0u);
    EXPECT_EQ(arena.m_current_offset, offset);
}

TEST_F(EditorHistoryTest, MixedSizeRingWrapAndRedoBranchStressMatchesReferenceHistory)
{
    std::vector<Change> model       = {};
    size_t              cursor      = 0;
    int                 next_value  = 1;
    int                 saved_value = 0;
    uint32_t            random      = 0x12345678u;
    auto                operation   = Operation();
    operation.Validate              = [](void* context, std::span<const std::byte> data, bool undo) {
        if (data.size() < sizeof(Change) || reinterpret_cast<uintptr_t>(data.data()) % alignof(std::max_align_t) != 0)
        {
            return false;
        }
        const auto& change = *reinterpret_cast<const Change*>(data.data());
        for (size_t i = sizeof(Change); i < data.size(); ++i)
        {
            if (data[i] != std::byte(change.After & 0xff))
            {
                return false;
            }
        }
        return *static_cast<int*>(context) == (undo ? change.After : change.Before);
    };

    const auto offset = arena.m_current_offset;
    for (int i = 0; i < 10000; ++i)
    {
        random            = random * 1664525u + 1013904223u;
        const auto action = (random >> 24) % 10;
        if (action < 6)
        {
            alignas(std::max_align_t) std::array<std::byte, 96> payload = {};
            const size_t                                        bytes   = (((random >> 8) % 6) + 1) * 16;
            const Change                                        change{value, next_value++};
            std::fill(payload.begin(), payload.begin() + bytes, std::byte(change.After & 0xff));
            std::construct_at(reinterpret_cast<Change*>(payload.data()), change);
            ASSERT_TRUE(history.ExecuteAndPush(&value, operation, std::span(payload).first(bytes)));
            model.resize(cursor);
            model.push_back(change);
            ASSERT_LE(history.UndoCount(), model.size());
            model.erase(model.begin(), model.begin() + (model.size() - history.UndoCount()));
            cursor = model.size();
            EXPECT_EQ(value, change.After);
        }
        else if (action < 8 && cursor)
        {
            ASSERT_TRUE(history.Undo(&value));
            EXPECT_EQ(value, model[--cursor].Before);
        }
        else if (action == 8 && cursor < model.size())
        {
            ASSERT_TRUE(history.Redo(&value));
            EXPECT_EQ(value, model[cursor++].After);
        }
        else if (action == 9)
        {
            history.MarkSaved();
            saved_value = value;
        }
        EXPECT_EQ(history.UndoCount(), cursor);
        EXPECT_EQ(history.RedoCount(), model.size() - cursor);
        EXPECT_LE(history.BytesUsed(), history.ByteCapacity());
        EXPECT_EQ(history.Dirty(), value != saved_value);
    }
    EXPECT_EQ(arena.m_current_offset, offset);
}
