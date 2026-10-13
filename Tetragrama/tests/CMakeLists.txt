add_executable(TetragramaTests EditorGizmoControllerTest.cpp)
target_link_libraries(TetragramaTests PRIVATE Tetragrama GTest::gtest_main)
set_target_properties(TetragramaTests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set_target_properties(TetragramaTests PROPERTIES INSTALL_RPATH "@loader_path/../lib")
    add_custom_command(TARGET TetragramaTests POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_FILE:TetragramaTests>)
else()
    set_target_properties(TetragramaTests PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif()
install(TARGETS TetragramaTests RUNTIME DESTINATION tests)
if(APPLE)
    # Installing rewrites the Mach-O rpath, invalidating the build-time signature.
    install(CODE [[
        execute_process(COMMAND /usr/bin/codesign --force --sign -
            "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/tests/TetragramaTests"
            COMMAND_ERROR_IS_FATAL ANY)
    ]])
endif()
include(GoogleTest)
gtest_discover_tests(TetragramaTests)
