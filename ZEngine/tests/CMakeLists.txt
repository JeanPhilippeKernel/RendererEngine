set (TEST_SOURCES
    bitmap_test.cpp
    IntrusivePtr_test.cpp
    IntrusiveWeakPtr_test.cpp
    MemoryOperation_test.cpp
    ThreadPool_test.cpp
)

add_executable(ZEngineTests ${TEST_SOURCES})

target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    GTest::gtest
    GTest::gtest_main
)
