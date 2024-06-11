#include <gtest/gtest.h>
#include "Helpers/ThreadPool.h"

using namespace ZEngine::Helpers;
 
class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
 
    void TearDown() override {
    }
};
 
TEST_F(ThreadPoolTest, Submit) {
    std::atomic<bool> executed = false;
    ThreadPoolHelper::Submit([&executed] {
        executed = true;
    });
 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
 
    EXPECT_TRUE(executed);
}
 
TEST_F(ThreadPoolTest, TaskExecution) {
    std::atomic<int> counter = 0;
    for (int i = 0; i < 5; ++i) {
        ThreadPoolHelper::Submit([&counter] {
            counter++;
        });
    }
 
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
 
    EXPECT_EQ(counter, 5);
}
 
TEST_F(ThreadPoolTest, MultipleTasksExecution) {
    std::atomic<int> counter = 0;
    const int numberOfTasks = 10;
 
    for (int i = 0; i < numberOfTasks; ++i) {
        ThreadPoolHelper::Submit([&counter] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
            ++counter;
        });
    }
 
    std::this_thread::sleep_for(std::chrono::seconds(2));
 
    EXPECT_EQ(counter, numberOfTasks);
}