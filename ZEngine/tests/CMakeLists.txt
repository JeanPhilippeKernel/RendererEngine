file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(ZEngineTests ${TEST_SOURCES})

target_include_directories(ZEngineTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    GTest::gtest
    GTest::gtest_main
)
