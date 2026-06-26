file ( GLOB TEST_SOURCES
            Memory/*.cpp
            Containers/*.cpp
            VFS/*.cpp
            Maths/*.cpp
            Misc/*.cpp)

add_executable(ZEngineTests)

target_sources(ZEngineTests PRIVATE ${TEST_SOURCES})

target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    GTest::gtest_main
)

set_target_properties(ZEngineTests PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)

install(TARGETS ZEngineTests 
    RUNTIME DESTINATION tests)

if(APPLE)
    add_custom_command(TARGET ZEngineTests POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_FILE:ZEngineTests>
        COMMENT "Ad-hoc signing ZEngineTests"
    )
endif()

if(BUILD_TESTING)
    include(GoogleTest)
    gtest_discover_tests(ZEngineTests)
endif()
