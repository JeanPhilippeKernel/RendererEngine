#include <Tetragrama/EditorSceneChange.h>
#include <gtest/gtest.h>
#include <type_traits>

using namespace Tetragrama;

namespace
{
    constexpr auto New  = SceneChangeKind::NewScene;
    constexpr auto Open = SceneChangeKind::OpenScene;
    class EditorSceneChangeTest : public testing::Test
    {
    protected:
        EditorSceneChange change = {};
        uint64_t          Prompt(SceneChangeKind kind = New, uint64_t epoch = 1, uint64_t revision = 7)
        {
            EXPECT_EQ(change.Evaluate(kind, epoch, revision, true), SceneChangeGate::Waiting);
            return change.Prompt().Serial;
        }
    };
    static_assert(std::is_trivially_copyable_v<EditorSceneChange> && sizeof(EditorSceneChange) <= 64);
} // namespace

TEST_F(EditorSceneChangeTest, DefaultRecordsAreEmptyAndInvalidInputsCannotAuthorize)
{
    const SceneChangePrompt prompt = {};
    EXPECT_FALSE(prompt.Visible());
    EXPECT_EQ(prompt.Epoch, 0u);
    EXPECT_EQ(prompt.Revision, 0u);
    EXPECT_EQ(prompt.ExternalRevision, 0u);
    EXPECT_EQ(prompt.Serial, 0u);
    EXPECT_FALSE(prompt.Saving);
    EXPECT_FALSE(prompt.SaveFailed);
    EXPECT_FALSE(change.Prompt().Visible());
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, 0));
    EXPECT_FALSE(change.MayDiscard(New, 1, 7));
    EXPECT_EQ(change.Evaluate(New, 0, 7, true), SceneChangeGate::Cancelled);
    EXPECT_EQ(change.Evaluate(SceneChangeKind::None, 1, 7, true), SceneChangeGate::Cancelled);
}

TEST_F(EditorSceneChangeTest, CleanDocumentNeedsNoPromptOrAuthorization)
{
    EXPECT_EQ(change.Evaluate(New, 1, 0, false), SceneChangeGate::Ready);
    EXPECT_EQ(change.Evaluate(Open, 1, 0, false), SceneChangeGate::Ready);
    EXPECT_FALSE(change.Prompt().Visible());
    EXPECT_FALSE(change.MayDiscard(New, 1, 0));
}

TEST_F(EditorSceneChangeTest, WaitingWithoutNewUiFramesDoesNotRegeneratePrompt)
{
    const auto serial = Prompt();
    ASSERT_NE(serial, 0u);
    for (uint32_t frame = 0; frame < 100; ++frame)
    {
        EXPECT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Waiting);
        EXPECT_EQ(change.Prompt().Serial, serial);
    }
    EXPECT_FALSE(change.MayDiscard(New, 1, 7));
}

TEST_F(EditorSceneChangeTest, DiscardAuthorizesOnlyTheOriginalSceneRevisionAndAction)
{
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Discard, Prompt()));
    EXPECT_FALSE(change.MayDiscard(New, 1, 7)); // response is only queued intent
    EXPECT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Ready);
    EXPECT_TRUE(change.MayDiscard(New, 1, 7));
    EXPECT_FALSE(change.MayDiscard(New, 2, 7));
    EXPECT_FALSE(change.MayDiscard(New, 1, 8));
    EXPECT_FALSE(change.MayDiscard(Open, 1, 7));
    EXPECT_FALSE(change.Prompt().Visible());
    EXPECT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Ready);
}

TEST_F(EditorSceneChangeTest, LaterEditRevokesAlreadyGrantedDiscard)
{
    const auto serial = Prompt();
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Discard, serial));
    ASSERT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Ready);
    EXPECT_EQ(change.Evaluate(New, 1, 8, true), SceneChangeGate::Waiting);
    EXPECT_FALSE(change.MayDiscard(New, 1, 7));
    EXPECT_FALSE(change.MayDiscard(New, 1, 8));
    EXPECT_NE(change.Prompt().Serial, serial);
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, serial));
}

TEST_F(EditorSceneChangeTest, DelayedSaveOrDiscardCannotApproveNewEdits)
{
    for (const auto choice : {SceneChangeChoice::Save, SceneChangeChoice::Discard})
    {
        change.Reset();
        const auto serial = Prompt();
        ASSERT_TRUE(change.Choose(choice, serial));
        EXPECT_EQ(change.Evaluate(New, 1, 8, true), SceneChangeGate::Waiting);
        EXPECT_EQ(change.Prompt().Revision, 8u);
        EXPECT_NE(change.Prompt().Serial, serial);
        EXPECT_FALSE(change.Prompt().Saving);
        EXPECT_FALSE(change.MayDiscard(New, 1, 8));
    }
}

TEST_F(EditorSceneChangeTest, LegacyEditsInvalidateQueuedAndGrantedConsentWithoutASessionRevisionChange)
{
    for (const auto choice : {SceneChangeChoice::Save, SceneChangeChoice::Discard})
    {
        change.Reset();
        ASSERT_EQ(change.Evaluate(New, 1, 7, true, 2), SceneChangeGate::Waiting);
        const auto serial = change.Prompt().Serial;
        ASSERT_TRUE(change.Choose(choice, serial));
        ASSERT_EQ(change.Evaluate(New, 1, 7, true, 3), SceneChangeGate::Waiting);
        EXPECT_EQ(change.Prompt().Revision, 7u);
        EXPECT_EQ(change.Prompt().ExternalRevision, 3u);
        EXPECT_NE(change.Prompt().Serial, serial);
        EXPECT_FALSE(change.Choose(choice, serial));
        EXPECT_FALSE(change.MayDiscard(New, 1, 7, 3));
    }
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Discard, change.Prompt().Serial));
    ASSERT_EQ(change.Evaluate(New, 1, 7, true, 3), SceneChangeGate::Ready);
    EXPECT_TRUE(change.MayDiscard(New, 1, 7, 3));
    EXPECT_FALSE(change.MayDiscard(New, 1, 7, 4));
    ASSERT_EQ(change.Evaluate(New, 1, 7, true, 4), SceneChangeGate::Waiting);
    EXPECT_FALSE(change.MayDiscard(New, 1, 7, 3));
    EXPECT_FALSE(change.MayDiscard(New, 1, 7, 4));
}

TEST_F(EditorSceneChangeTest, FailedSavePreservesTheLegacyRevisionUntilARealEdit)
{
    ASSERT_EQ(change.Evaluate(Open, 1, 7, true, 2), SceneChangeGate::Waiting);
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Save, change.Prompt().Serial));
    ASSERT_EQ(change.Evaluate(Open, 1, 7, true, 2), SceneChangeGate::Save);
    change.SaveFinished(1, false);
    EXPECT_EQ(change.Prompt().ExternalRevision, 2u);
    EXPECT_TRUE(change.Prompt().SaveFailed);
    const auto serial = change.Prompt().Serial;
    ASSERT_EQ(change.Evaluate(Open, 1, 7, true, 2), SceneChangeGate::Waiting);
    EXPECT_EQ(change.Prompt().Serial, serial);
    ASSERT_EQ(change.Evaluate(Open, 1, 7, true, 3), SceneChangeGate::Waiting);
    EXPECT_NE(change.Prompt().Serial, serial);
    EXPECT_FALSE(change.Prompt().SaveFailed);
}

TEST_F(EditorSceneChangeTest, SaveIsRequestedOnceAndActualCompletionIsRequired)
{
    const auto serial = Prompt();
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Save, serial));
    EXPECT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Save);
    EXPECT_TRUE(change.Prompt().Saving);
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Save, serial));
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, serial));
    for (bool dirty : {true, false})
    {
        EXPECT_EQ(change.Evaluate(New, 1, 7, dirty), SceneChangeGate::Waiting);
    }
    change.SaveFinished(2, true); // wrong epoch cannot complete this save
    EXPECT_TRUE(change.Prompt().Saving);
    change.SaveFinished(1, true);
    EXPECT_EQ(change.Evaluate(New, 1, 7, false), SceneChangeGate::Ready);
    EXPECT_FALSE(change.Prompt().Visible());
}

TEST_F(EditorSceneChangeTest, EditAfterSuccessfulSaveRequiresFreshConsent)
{
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Save, Prompt()));
    ASSERT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Save);
    change.SaveFinished(1, true);
    EXPECT_EQ(change.Evaluate(New, 1, 8, true), SceneChangeGate::Waiting);
    EXPECT_FALSE(change.MayDiscard(New, 1, 8));
}

TEST_F(EditorSceneChangeTest, FailedSaveRepromptsWithANewSerialAndNeverAuthorizesDiscard)
{
    const auto serial = Prompt();
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Save, serial));
    ASSERT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Save);
    change.SaveFinished(1, false);
    EXPECT_TRUE(change.Prompt().SaveFailed);
    EXPECT_FALSE(change.Prompt().Saving);
    EXPECT_NE(change.Prompt().Serial, serial);
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, serial));
    EXPECT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Waiting);
    EXPECT_FALSE(change.MayDiscard(New, 1, 7));
}

TEST_F(EditorSceneChangeTest, CancelWinsOverLaterSaveSuccessOrCleanCheckpoint)
{
    for (bool saving : {false, true})
    {
        const auto serial = Prompt();
        if (saving)
        {
            ASSERT_TRUE(change.Choose(SceneChangeChoice::Save, serial));
            ASSERT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Save);
        }
        ASSERT_TRUE(change.Choose(SceneChangeChoice::Cancel, serial));
        change.SaveFinished(1, true);
        EXPECT_EQ(change.Evaluate(New, 1, 7, false), SceneChangeGate::Cancelled);
        EXPECT_FALSE(change.Prompt().Visible());
    }
}

TEST_F(EditorSceneChangeTest, CancelledNativeSavePickerCancelsOnlyTheMatchingTransition)
{
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Save, Prompt()));
    ASSERT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Save);
    change.SaveCancelled(2);
    EXPECT_TRUE(change.Prompt().Saving);
    change.SaveCancelled(1);
    EXPECT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Cancelled);
    EXPECT_FALSE(change.MayDiscard(New, 1, 7));
}

TEST_F(EditorSceneChangeTest, ReplacementEpochOrActionCannotReuseAnOldResponse)
{
    const auto serial = Prompt();
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Discard, serial));
    EXPECT_EQ(change.Evaluate(Open, 2, 7, true), SceneChangeGate::Waiting);
    EXPECT_EQ(change.Prompt().Kind, Open);
    EXPECT_EQ(change.Prompt().Epoch, 2u);
    EXPECT_NE(change.Prompt().Serial, serial);
    EXPECT_FALSE(change.MayDiscard(Open, 2, 7));
}

TEST_F(EditorSceneChangeTest, ResetAndFinishNeverReuseSerialsOrRetireAnotherRequest)
{
    const auto first = Prompt();
    change.Finish(Open, 1);
    EXPECT_EQ(change.Prompt().Serial, first);
    change.Finish(New, 1);
    EXPECT_FALSE(change.Prompt().Visible());
    const auto second = Prompt();
    EXPECT_GT(second, first);
    change.Reset();
    EXPECT_GT(Prompt(), second);
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, first));
}

TEST_F(EditorSceneChangeTest, InvalidAndRepeatedChoicesDoNotOverwriteTheOriginalIntent)
{
    const auto serial = Prompt();
    EXPECT_FALSE(change.Choose(SceneChangeChoice::None, serial));
    EXPECT_FALSE(change.Choose(static_cast<SceneChangeChoice>(255), serial));
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, serial + 1));
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Cancel, serial));
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, serial));
    EXPECT_EQ(change.Evaluate(New, 1, 7, true), SceneChangeGate::Cancelled);
}

TEST_F(EditorSceneChangeTest, CloseEditorUsesTheSameRevisionBoundConsentWithoutApprovingAnotherAction)
{
    constexpr auto close = SceneChangeKind::CloseEditor;
    EXPECT_EQ(change.Evaluate(close, 1, 0, false), SceneChangeGate::Ready);
    const auto serial = Prompt(close);
    ASSERT_TRUE(change.Choose(SceneChangeChoice::Discard, serial));
    EXPECT_EQ(change.Evaluate(close, 1, 7, true), SceneChangeGate::Ready);
    EXPECT_TRUE(change.MayDiscard(close, 1, 7));
    EXPECT_FALSE(change.MayDiscard(New, 1, 7));
    EXPECT_FALSE(change.MayDiscard(Open, 1, 7));
    EXPECT_EQ(change.Evaluate(close, 1, 8, true), SceneChangeGate::Waiting);
    EXPECT_FALSE(change.MayDiscard(close, 1, 8));
    EXPECT_FALSE(change.Choose(SceneChangeChoice::Discard, serial));
}
