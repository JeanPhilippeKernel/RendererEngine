#include <gtest/gtest.h>
#include <latch>
#include "Helpers/ThreadPool.h"
using namespace ZEngine::Helpers;
 
class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
 
    void TearDown() override {
        ThreadPoolHelper::Shutdown();
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
    const int tasks = 5;
    std::latch latch(tasks);

    for (int i = 0; i < tasks; ++i) {
        ThreadPoolHelper::Submit([&counter, &latch] {
            counter++;
            latch.count_down();
        });
    }

    latch.wait(); 

    EXPECT_EQ(counter, tasks);
}
 
TEST_F(ThreadPoolTest, MultipleTasksExecution) {
    std::atomic<int> counter = 0;
    const int numberOfTasks = 10;
    std::latch latch(numberOfTasks);

    for (int i = 0; i < numberOfTasks; ++i) {
        ThreadPoolHelper::Submit([&counter, &latch] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ++counter;
            latch.count_down();
        });
    }

    latch.wait(); 

    EXPECT_EQ(counter, numberOfTasks);
}
