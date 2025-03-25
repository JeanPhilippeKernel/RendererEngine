set (TEST_SOURCES
    bitmap_test.cpp
    IntrusivePtr_test.cpp
    IntrusiveWeakPtr_test.cpp
    MemoryOperation_test.cpp
    ThreadPool_test.cpp
    handleManager_test.cpp
    allocator_test.cpp
    array_test.cpp
    string_test.cpp
    hashmap_test.cpp
)

add_executable(ZEngineTests ${TEST_SOURCES})

target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    GTest::gtest
    GTest::gtest_main
)
