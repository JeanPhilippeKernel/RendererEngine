file ( GLOB TEST_SOURCES 
            Memory/*.cpp
            Containers/*.cpp
            Maths/*.cpp
            Misc/*.cpp)

add_executable(ZEngineTests)

target_sources(ZEngineTests PRIVATE ${TEST_SOURCES})

target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    GTest::gtest_main
)
