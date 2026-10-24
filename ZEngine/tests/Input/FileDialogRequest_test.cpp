#include <ZEngine/Windows/FileDialogRequest.h>
#include <gtest/gtest.h>
#include <thread>

using namespace ZEngine::Windows;

TEST(FileDialogRequestTest, DefaultAndPendingRequestsDoNotProduceResults)
{
    FileDialogRequest request;
    FileDialogResult  result = {};
    EXPECT_FALSE(request.valid());
    EXPECT_FALSE(request.TryTake(&result));
    request = FileDialogRequest::Create();
    EXPECT_TRUE(request.valid());
    EXPECT_FALSE(request.TryTake(&result));
    EXPECT_FALSE(request.TryTake(nullptr));
}

TEST(FileDialogRequestTest, SelectedCancelledAndFailedAreDistinctAndConsumedOnce)
{
    for (const auto status : {FileDialogStatus::Selected, FileDialogStatus::Cancelled, FileDialogStatus::Failed})
    {
        auto producer = FileDialogRequest::Create();
        auto consumer = producer;
        if (status == FileDialogStatus::Failed)
            EXPECT_TRUE(producer.Fail());
        else
            EXPECT_TRUE(producer.Complete(status == FileDialogStatus::Selected ? "/Project/Scenes/main.zescene" : ""));
        EXPECT_FALSE(producer.Complete("overwrite"));
        EXPECT_FALSE(producer.Fail());
        FileDialogResult result = {};
        ASSERT_TRUE(consumer.TryTake(&result));
        EXPECT_EQ(result.Outcome, status);
        EXPECT_EQ(result.Path.empty(), status != FileDialogStatus::Selected);
        EXPECT_FALSE(consumer.valid());
        EXPECT_FALSE(consumer.TryTake(&result));
    }
}

TEST(FileDialogRequestTest, CompletionSurvivesConsumerAbandonment)
{
    auto producer = FileDialogRequest::Create();
    {
        auto abandoned = producer;
    }
    EXPECT_TRUE(producer.Complete("/Project/Scenes/main.zescene"));
    FileDialogResult result = {};
    EXPECT_TRUE(producer.TryTake(&result));
}

TEST(FileDialogRequestTest, WorkerPublishesOwnedPathBeforeReady)
{
    auto        producer = FileDialogRequest::Create();
    auto        consumer = producer;
    std::thread worker([producer] { EXPECT_TRUE(producer.Complete(std::string(4096, 'a'))); });
    worker.join();
    FileDialogResult result = {};
    ASSERT_TRUE(consumer.TryTake(&result));
    EXPECT_EQ(result.Outcome, FileDialogStatus::Selected);
    EXPECT_EQ(result.Path, std::string(4096, 'a'));
}
