file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(ZEngineTests ${TEST_SOURCES})

target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    vulkan
    GTest::gtest
    GTest::gtest_main
)
