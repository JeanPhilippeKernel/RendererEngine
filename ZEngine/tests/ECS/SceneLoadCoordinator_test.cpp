#include <ZEngine/ECS/SceneLoadCoordinator.h>
#include <gtest/gtest.h>
#include <thread>

using namespace ZEngine::ECS;

TEST(SceneLoadCoordinatorTest, InvalidAdmissionPreservesExistingRequestAndOutput)
{
    SceneLoadCoordinator loads = {};
    SceneRequestId       first = {}, rejected = {};
    ASSERT_TRUE(loads.Admit(1, &first));
    rejected = first;
    EXPECT_FALSE(loads.Admit(0, &rejected));
    EXPECT_FALSE(loads.Admit(std::numeric_limits<uint64_t>::max(), &rejected));
    EXPECT_FALSE(loads.Admit(1, nullptr));
    EXPECT_EQ(rejected, first);
    EXPECT_TRUE(loads.Pending(first));
    EXPECT_FALSE(loads.Acknowledge(first));
}

TEST(SceneLoadCoordinatorTest, RapidRequestsRetainOnePreparationAndOnlyTheLatestWaitingRequest)
{
    SceneLoadCoordinator loads = {};
    SceneRequestId       a = {}, b = {}, c = {};
    SceneLoadResult      result = {};
    ASSERT_TRUE(loads.Admit(7, &a));
    ASSERT_TRUE(loads.TryStart(a));
    ASSERT_TRUE(loads.Admit(7, &b));
    ASSERT_TRUE(loads.Admit(7, &c));
    ASSERT_TRUE(loads.Poll(a, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Superseded);
    EXPECT_TRUE(result.CleanupPending);
    EXPECT_FALSE(loads.BeginCommit(a, 7));
    EXPECT_FALSE(loads.TryStart(b));
    EXPECT_FALSE(loads.TryStart(c));
    EXPECT_EQ(loads.Cancel(a), SceneCancelResult::AlreadyTerminal);
    EXPECT_FALSE(loads.Fail(a, SceneLoadStage::Read, SceneLoadError::Source));
    ASSERT_TRUE(loads.Poll(b, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Superseded);
    EXPECT_FALSE(result.CleanupPending);
    ASSERT_TRUE(loads.Acknowledge(a));
    EXPECT_FALSE(loads.Poll(a, &result));
    EXPECT_FALSE(loads.TryStart(c));
    ASSERT_TRUE(loads.FinishCleanup(a));
    ASSERT_TRUE(loads.TryStart(c));
    EXPECT_TRUE(loads.Acknowledge(b));
}

TEST(SceneLoadCoordinatorTest, FullResultCapacityRejectsWithoutSupersedingLatestAndReleaseRestoresAdmission)
{
    SceneLoadCoordinator loads                                          = {};
    SceneRequestId       requests[SceneLoadCoordinator::ResultCapacity] = {};
    SceneLoadResult      result                                         = {};
    for (auto& request : requests)
    {
        ASSERT_TRUE(loads.Admit(1, &request));
    }
    SceneRequestId rejected = {};
    EXPECT_FALSE(loads.Admit(1, &rejected));
    EXPECT_FALSE(static_cast<bool>(rejected));
    EXPECT_TRUE(loads.Pending(requests[SceneLoadCoordinator::ResultCapacity - 1]));
    ASSERT_TRUE(loads.Acknowledge(requests[0]));
    EXPECT_FALSE(loads.Poll(requests[0], &result));
    ASSERT_TRUE(loads.Admit(1, &rejected));
    EXPECT_NE(rejected, requests[0]);
    EXPECT_FALSE(loads.TryStart(requests[0]));
}

TEST(SceneLoadCoordinatorTest, CancellationBeforeCommitWinsAndAcknowledgementDoesNotReleaseCleanupCapacity)
{
    SceneLoadCoordinator loads = {};
    SceneRequestId       a = {}, b = {};
    ASSERT_TRUE(loads.Admit(1, &a));
    ASSERT_TRUE(loads.TryStart(a));
    EXPECT_EQ(loads.Cancel(a), SceneCancelResult::Cancelled);
    EXPECT_EQ(loads.Cancel(a), SceneCancelResult::AlreadyTerminal);
    EXPECT_FALSE(loads.BeginCommit(a, 1));
    EXPECT_TRUE(loads.Acknowledge(a));
    EXPECT_FALSE(loads.Drained());
    ASSERT_TRUE(loads.Admit(1, &b));
    EXPECT_FALSE(loads.TryStart(b));
    EXPECT_TRUE(loads.FinishCleanup(a));
    EXPECT_FALSE(loads.FinishCleanup(a));
    EXPECT_TRUE(loads.TryStart(b));
}

TEST(SceneLoadCoordinatorTest, CommitIsGenerationGuardedAndCancellationAfterEntryReportsTooLate)
{
    SceneLoadCoordinator loads   = {};
    SceneRequestId       request = {}, rejected = {};
    SceneLoadResult      result = {};
    ASSERT_TRUE(loads.Admit(17, &request));
    ASSERT_TRUE(loads.TryStart(request));
    EXPECT_FALSE(loads.BeginCommit(request, 18));
    ASSERT_TRUE(loads.BeginCommit(request, 17));
    EXPECT_EQ(loads.Cancel(request), SceneCancelResult::TooLate);
    EXPECT_FALSE(loads.Admit(17, &rejected));
    EXPECT_FALSE(loads.Fail(request, SceneLoadStage::Publication, SceneLoadError::Invalidated));
    EXPECT_FALSE(loads.Commit(request, 19));
    ASSERT_TRUE(loads.Commit(request, 18));
    EXPECT_FALSE(loads.Commit(request, 18));
    EXPECT_EQ(loads.Cancel(request), SceneCancelResult::TooLate);
    EXPECT_TRUE(loads.FinishCleanup(request));
    ASSERT_TRUE(loads.Poll(request, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Committed);
    EXPECT_EQ(result.CommittedGeneration, 18u);
    EXPECT_FALSE(result.CleanupPending);
    EXPECT_TRUE(loads.Acknowledge(request));
}

TEST(SceneLoadCoordinatorTest, StableFailureAndShutdownCannotOverwriteTheFirstTerminalOutcome)
{
    SceneLoadCoordinator loads   = {};
    SceneRequestId       request = {}, rejected = {};
    SceneLoadResult      result = {};
    ASSERT_TRUE(loads.Admit(1, &request));
    ASSERT_TRUE(loads.TryStart(request));
    ASSERT_TRUE(loads.Fail(request, SceneLoadStage::Read, SceneLoadError::Source));
    loads.Shutdown();
    EXPECT_FALSE(loads.Drained());
    EXPECT_FALSE(loads.Admit(1, &rejected));
    EXPECT_FALSE(loads.Fail(request, SceneLoadStage::Assets, SceneLoadError::Resources));
    ASSERT_TRUE(loads.Poll(request, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Failed);
    EXPECT_EQ(result.Stage, SceneLoadStage::Read);
    EXPECT_EQ(result.Error, SceneLoadError::Source);
    EXPECT_TRUE(result.CleanupPending);
    EXPECT_TRUE(loads.FinishCleanup(request));
    EXPECT_TRUE(loads.Drained());
}

TEST(SceneLoadCoordinatorTest, ShutdownCancelsWaitingAndPreparingButRetainsUnjoinedPayloads)
{
    SceneLoadCoordinator loads     = {};
    SceneRequestId       preparing = {}, waiting = {};
    SceneLoadResult      result = {};
    ASSERT_TRUE(loads.Admit(1, &preparing));
    ASSERT_TRUE(loads.TryStart(preparing));
    ASSERT_TRUE(loads.Admit(1, &waiting));
    loads.Shutdown();
    ASSERT_TRUE(loads.Poll(preparing, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Superseded);
    EXPECT_TRUE(result.CleanupPending);
    ASSERT_TRUE(loads.Poll(waiting, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Cancelled);
    EXPECT_FALSE(result.CleanupPending);
    EXPECT_FALSE(loads.TryStart(waiting));
    EXPECT_TRUE(loads.Acknowledge(waiting));
    EXPECT_TRUE(loads.FinishCleanup(preparing));
}

TEST(SceneLoadCoordinatorTest, WorkerSaturationCanDeferOnlyAnUnsubmittedPreparation)
{
    SceneLoadCoordinator loads   = {};
    SceneRequestId       request = {};
    SceneLoadResult      result  = {};
    ASSERT_TRUE(loads.Admit(1, &request));
    EXPECT_FALSE(loads.DeferUnsubmitted(request));
    ASSERT_TRUE(loads.TryStart(request));
    EXPECT_TRUE(loads.DeferUnsubmitted(request));
    ASSERT_TRUE(loads.Poll(request, &result));
    EXPECT_EQ(result.Outcome, SceneLoadOutcome::Pending);
    EXPECT_FALSE(result.CleanupPending);
    ASSERT_TRUE(loads.TryStart(request));
    ASSERT_TRUE(loads.BeginCommit(request, 1));
    EXPECT_FALSE(loads.DeferUnsubmitted(request));
}

TEST(SceneLoadCoordinatorTest, ForeignLifetimeAndOffThreadCallsCannotMutateTheCoordinator)
{
    SceneLoadCoordinator first = {}, second = {};
    SceneRequestId       request = {}, foreign = {};
    SceneLoadResult      result = {};
    ASSERT_TRUE(first.Admit(1, &request));
    ASSERT_TRUE(second.Admit(1, &foreign));
    EXPECT_NE(request.Lifetime, foreign.Lifetime);
    EXPECT_EQ(first.Cancel(foreign), SceneCancelResult::NotFound);
    EXPECT_FALSE(first.Poll(foreign, &result));
    std::thread worker([&] {
        EXPECT_FALSE(first.Admit(1, &foreign));
        EXPECT_FALSE(first.TryStart(request));
        EXPECT_EQ(first.Cancel(request), SceneCancelResult::NotFound);
        EXPECT_FALSE(first.Poll(request, &result));
        first.Shutdown();
    });
    worker.join();
    EXPECT_TRUE(first.Pending(request));
    EXPECT_TRUE(first.TryStart(request));
}

TEST(SceneLoadCoordinatorTest, RepeatedCompletionRecyclesBoundedRecordsWithoutReusingRequestIdentities)
{
    SceneLoadCoordinator loads    = {};
    SceneRequestId       previous = {};
    for (uint64_t generation = 1; generation <= 1100; ++generation)
    {
        SceneRequestId request = {};
        ASSERT_TRUE(loads.Admit(generation, &request));
        EXPECT_GT(request.Sequence, previous.Sequence);
        EXPECT_FALSE(loads.TryStart(previous));
        ASSERT_TRUE(loads.TryStart(request));
        ASSERT_TRUE(loads.BeginCommit(request, generation));
        ASSERT_TRUE(loads.Commit(request, generation + 1));
        ASSERT_TRUE(loads.Acknowledge(request));
        ASSERT_TRUE(loads.FinishCleanup(request));
        EXPECT_TRUE(loads.Drained());
        previous = request;
    }
}
