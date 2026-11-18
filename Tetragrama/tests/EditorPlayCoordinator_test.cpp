#include <Tetragrama/EditorPlayCoordinator.h>
#include <gtest/gtest.h>
#include <thread>

using Tetragrama::EditorPlayCoordinator;
using namespace ZEngine::ECS;

namespace
{
    using Mode = EditorPlayCoordinator::Mode;

    /// @brief Commit an Enter and transfer its prepared payload to the host.
    SceneRequestId Enter(EditorPlayCoordinator& coordinator)
    {
        SceneRequestId request = {};
        EXPECT_TRUE(coordinator.BeginEnter(&request));
        EXPECT_TRUE(coordinator.BeginEnterCommit(request, coordinator.Generation()));
        EXPECT_TRUE(coordinator.FinishEnterCommit(request));
        EXPECT_TRUE(coordinator.FinishCleanup(request));
        return request;
    }
} // namespace

TEST(EditorPlayCoordinatorTest, EnterPauseStepAndStopAdvanceOnlyOnCommittedSwitches)
{
    EditorPlayCoordinator coordinator = {};
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Edit);
    EXPECT_EQ(coordinator.Generation(), 1u);
    EXPECT_FALSE(coordinator.Pause());
    EXPECT_FALSE(coordinator.BeginStep());
    const auto enter = Enter(coordinator);
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Play);
    EXPECT_EQ(coordinator.Generation(), 2u);
    EXPECT_EQ(coordinator.CancelEnter(enter), SceneCancelResult::TooLate);
    ASSERT_TRUE(coordinator.Pause());
    ASSERT_TRUE(coordinator.BeginStep());
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Stepping);
    EXPECT_FALSE(coordinator.BeginStep());
    EXPECT_FALSE(coordinator.Resume());
    SceneRequestId stop = {};
    EXPECT_FALSE(coordinator.BeginStop(&stop));
    ASSERT_TRUE(coordinator.FinishStep());
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Paused);
    ASSERT_TRUE(coordinator.Resume());
    ASSERT_TRUE(coordinator.BeginStop(&stop));
    EXPECT_EQ(coordinator.CancelEnter(stop), SceneCancelResult::NotFound);
    EXPECT_FALSE(coordinator.BeginStopCommit(stop, 1));
    ASSERT_TRUE(coordinator.BeginStopCommit(stop, 2));
    ASSERT_TRUE(coordinator.FinishStopCommit(stop));
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Edit);
    EXPECT_EQ(coordinator.Generation(), 3u);
    EXPECT_TRUE(coordinator.FinishCleanup(stop));
    EXPECT_TRUE(coordinator.Acknowledge(stop));
    EXPECT_TRUE(coordinator.Acknowledge(enter));
}

TEST(EditorPlayCoordinatorTest, CancelledEnterRetainsCleanupAfterAcknowledgement)
{
    EditorPlayCoordinator coordinator = {};
    SceneRequestId        request     = {};
    ASSERT_TRUE(coordinator.BeginEnter(&request));
    EXPECT_EQ(coordinator.CancelEnter(request), SceneCancelResult::Cancelled);
    SceneLoadResult result = {};
    ASSERT_TRUE(coordinator.Poll(request, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Cancelled);
    EXPECT_TRUE(result.CleanupPending);
    ASSERT_TRUE(coordinator.Acknowledge(request));
    SceneRequestId unchanged{9, 9};
    EXPECT_FALSE(coordinator.BeginEnter(&unchanged));
    EXPECT_EQ(unchanged, (SceneRequestId{9, 9}));
    ASSERT_TRUE(coordinator.FinishCleanup(request));
    EXPECT_FALSE(coordinator.FinishCleanup(request));
    ASSERT_TRUE(coordinator.BeginEnter(&unchanged));
    EXPECT_NE(unchanged, request);
}

TEST(EditorPlayCoordinatorTest, CommitGuardRejectsStaleGenerationAndMakesCancellationTooLate)
{
    EditorPlayCoordinator coordinator = {};
    SceneRequestId        request     = {};
    ASSERT_TRUE(coordinator.BeginEnter(&request));
    EXPECT_FALSE(coordinator.FinishEnterCommit(request));
    EXPECT_FALSE(coordinator.BeginEnterCommit(request, 2));
    ASSERT_TRUE(coordinator.BeginEnterCommit(request, 1));
    EXPECT_EQ(coordinator.CancelEnter(request), SceneCancelResult::TooLate);
    EXPECT_FALSE(coordinator.FailEnter(request, SceneLoadStage::Publication, SceneLoadError::Resources));
    EXPECT_FALSE(coordinator.BeginEnterCommit(request, 1));
    ASSERT_TRUE(coordinator.FinishEnterCommit(request));
    EXPECT_FALSE(coordinator.FinishEnterCommit(request));
    SceneLoadResult result = {};
    ASSERT_TRUE(coordinator.Poll(request, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Committed);
    EXPECT_EQ(result.CommittedGeneration, 2u);
}

TEST(EditorPlayCoordinatorTest, FailedStopRequiresCleanupBeforeRetry)
{
    EditorPlayCoordinator coordinator = {};
    const auto            enter       = Enter(coordinator);
    SceneRequestId        stop        = {};
    ASSERT_TRUE(coordinator.BeginStop(&stop));
    ASSERT_TRUE(coordinator.FailStop(stop, SceneLoadStage::Preparation, SceneLoadError::Resources));
    EXPECT_EQ(coordinator.CurrentMode(), Mode::ReturnError);
    EXPECT_EQ(coordinator.Generation(), 2u);
    EXPECT_FALSE(coordinator.Pause());
    SceneRequestId rejected_enter = {};
    EXPECT_FALSE(coordinator.BeginEnter(&rejected_enter));
    SceneRequestId retry = {};
    EXPECT_FALSE(coordinator.BeginStop(&retry));
    ASSERT_TRUE(coordinator.FinishCleanup(stop));
    ASSERT_TRUE(coordinator.BeginStop(&retry));
    EXPECT_NE(retry, stop);
    EXPECT_FALSE(coordinator.FinishStopCommit(stop));
    ASSERT_TRUE(coordinator.BeginStopCommit(retry, 2));
    ASSERT_TRUE(coordinator.FinishStopCommit(retry));
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Edit);
    EXPECT_EQ(coordinator.Generation(), 3u);
}

TEST(EditorPlayCoordinatorTest, UnsupportedEnterAndForeignThreadLeaveStateUnchanged)
{
    EditorPlayCoordinator coordinator     = {};
    SceneRequestId        request         = {};
    bool                  worker_accepted = true;
    std::thread           worker([&] { worker_accepted = coordinator.BeginEnter(&request); });
    worker.join();
    EXPECT_FALSE(worker_accepted);
    ASSERT_TRUE(coordinator.BeginEnter(&request));
    EXPECT_FALSE(coordinator.FailEnter(request, SceneLoadStage::None, SceneLoadError::UnsupportedCapability));
    ASSERT_TRUE(coordinator.FailEnter(request, SceneLoadStage::Admission, SceneLoadError::UnsupportedCapability));
    SceneLoadResult result = {};
    ASSERT_TRUE(coordinator.Poll(request, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Failed);
    EXPECT_EQ(result.Error, SceneLoadError::UnsupportedCapability);
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Edit);
    EXPECT_EQ(coordinator.Generation(), 1u);
}

TEST(EditorPlayCoordinatorTest, UnacknowledgedResultsApplyBoundedBackpressure)
{
    EditorPlayCoordinator coordinator                                    = {};
    SceneRequestId        requests[SceneLoadCoordinator::ResultCapacity] = {};
    for (auto& request : requests)
    {
        ASSERT_TRUE(coordinator.BeginEnter(&request));
        ASSERT_EQ(coordinator.CancelEnter(request), SceneCancelResult::Cancelled);
        ASSERT_TRUE(coordinator.FinishCleanup(request));
    }
    SceneRequestId output{7, 7};
    EXPECT_FALSE(coordinator.BeginEnter(&output));
    EXPECT_EQ(output, (SceneRequestId{7, 7}));
    EXPECT_EQ(coordinator.CurrentMode(), Mode::Edit);
    ASSERT_TRUE(coordinator.Acknowledge(requests[0]));
    EXPECT_TRUE(coordinator.BeginEnter(&output));
}
