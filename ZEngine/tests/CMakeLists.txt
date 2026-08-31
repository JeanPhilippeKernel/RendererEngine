file(GLOB TEST_SOURCES
    Memory/*.cpp
    Containers/*.cpp
    VFS/*.cpp
    Maths/*.cpp
    Misc/*.cpp
    Logging/*.cpp
    Input/*.cpp
    ECS/*.cpp
    Rendering/*.cpp
)

# macOS and Windows are multi-config
if (((APPLE) OR (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")))
    if ("Release" IN_LIST CMAKE_CONFIGURATION_TYPES)
        list(APPEND TEST_SOURCES CrashHandlers/CrashHandler_test.cpp)
        add_executable(ZEngineCrashDialogDemo CrashHandlers/CrashDialogDemo.cpp)
        target_link_libraries(ZEngineCrashDialogDemo PRIVATE zEngineLib)
        if(APPLE)
            add_custom_command(TARGET ZEngineCrashDialogDemo POST_BUILD
                COMMAND codesign --force --sign - $<TARGET_FILE:ZEngineCrashDialogDemo>
                COMMENT "Ad-hoc signing ZEngineCrashDialogDemo"
            )
        endif()
    endif()
elseif((CMAKE_BUILD_TYPE STREQUAL "Release") OR (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    list(APPEND TEST_SOURCES CrashHandlers/CrashHandler_test.cpp)
    add_executable(ZEngineCrashDialogDemo CrashHandlers/CrashDialogDemo.cpp)
    target_link_libraries(ZEngineCrashDialogDemo PRIVATE zEngineLib)
endif()

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
    # freetype builds as a .framework bundle in _deps; install it into lib/ alongside
    # the vulkan dylibs so the binary's INSTALL_RPATH (lib/) resolves it on CI.
    install(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/freetype-build/$<CONFIG>/freetype.framework"
        DESTINATION lib
        USE_SOURCE_PERMISSIONS)
endif()

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
