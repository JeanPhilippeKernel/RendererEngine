file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    Memory/*.cpp
    Containers/*.cpp
    VFS/*.cpp
    Maths/*.cpp
    Misc/*.cpp
    Logging/*.cpp
    Input/*.cpp
    ECS/*.cpp
    Scene/*.cpp
    Rendering/*.cpp
    UI/*.cpp
)

if(APPLE)
    list(APPEND TEST_SOURCES Input/MacOSFileDialog_test.mm)
endif()

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

# Embed the real production GLSL, not a second test shader. Installed test
# executables then need neither the source tree nor a runtime fixture directory.
set(GIZMO_VERTEX_SOURCE "${CMAKE_SOURCE_DIR}/Resources/Shaders/gizmo.vert")
set(GIZMO_FRAGMENT_SOURCE "${CMAKE_SOURCE_DIR}/Resources/Shaders/gizmo.frag")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GIZMO_VERTEX_SOURCE}" "${GIZMO_FRAGMENT_SOURCE}")
file(READ "${GIZMO_VERTEX_SOURCE}" GIZMO_VERTEX_SOURCE)
file(READ "${GIZMO_FRAGMENT_SOURCE}" GIZMO_FRAGMENT_SOURCE)
configure_file(Rendering/GizmoShaderSources.h.in "generated/GizmoShaderSources.h" @ONLY)
set(GRID_LABEL_VERTEX_SOURCE "${CMAKE_SOURCE_DIR}/Resources/Shaders/grid_labels.vert")
set(GRID_LABEL_FRAGMENT_SOURCE "${CMAKE_SOURCE_DIR}/Resources/Shaders/grid_labels.frag")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GRID_LABEL_VERTEX_SOURCE}" "${GRID_LABEL_FRAGMENT_SOURCE}")
file(READ "${GRID_LABEL_VERTEX_SOURCE}" GRID_LABEL_VERTEX_SOURCE)
file(READ "${GRID_LABEL_FRAGMENT_SOURCE}" GRID_LABEL_FRAGMENT_SOURCE)
configure_file(Rendering/GridLabelShaderSources.h.in "generated/GridLabelShaderSources.h" @ONLY)
set_property(SOURCE Rendering/GpuAllocatorTest.cpp APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    GTest::gtest_main
)

add_executable(SceneDocumentProcess Scene/Process/SceneDocumentProcess.cpp)
target_link_libraries(SceneDocumentProcess PRIVATE zEngineLib GTest::gtest)

foreach(test_target ZEngineTests SceneDocumentProcess)
    if(APPLE)
        # @loader_path/../lib resolves to lib/ relative to wherever the binary lands,
        # surviving the artifact prefix-stripping that makes absolute CMAKE_INSTALL_PREFIX
        # paths wrong on the CI runner after download.
        set_target_properties(${test_target} PROPERTIES INSTALL_RPATH "@loader_path/../lib")
    else()
        set_target_properties(${test_target} PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)
    endif()
endforeach()

install(TARGETS ZEngineTests SceneDocumentProcess
    RUNTIME DESTINATION tests)
if(APPLE)
    # Installation rewrites rpaths after POST_BUILD signing. Re-sign the installed
    # copies so arm64 macOS can execute the downloaded CI artifacts.
    foreach(test_target ZEngineTests SceneDocumentProcess)
        install(CODE "
            execute_process(COMMAND /usr/bin/codesign --force --sign -
                \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/tests/${test_target}\"
                RESULT_VARIABLE sign_result ERROR_VARIABLE sign_error)
            if(NOT sign_result EQUAL 0)
                message(FATAL_ERROR \"Failed to sign installed ${test_target}: \${sign_error}\")
            endif()
        ")
    endforeach()
endif()
install(FILES Scene/Process/SceneDocumentProcess.cmake DESTINATION tests)

if(APPLE)
    # Install freetype.framework alongside vulkan in lib/ so the @loader_path/../lib
    # rpath entry resolves @rpath/freetype.framework/... correctly on CI.
    install(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/freetype-build/$<CONFIG>/freetype.framework"
        DESTINATION lib
        USE_SOURCE_PERMISSIONS)
endif()

if(APPLE)
    foreach(test_target ZEngineTests SceneDocumentProcess)
        add_custom_command(TARGET ${test_target} POST_BUILD
            COMMAND codesign --force --sign - $<TARGET_FILE:${test_target}>
            COMMENT "Ad-hoc signing ${test_target}"
        )
    endforeach()
endif()

if(BUILD_TESTING)
    include(GoogleTest)
    gtest_discover_tests(ZEngineTests)
    foreach(root_kind plain unicode)
        add_test(NAME SceneDocumentProcess.${root_kind}
            COMMAND ${CMAKE_COMMAND}
                "-DHELPER=$<TARGET_FILE:SceneDocumentProcess>"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/scene-process-$<CONFIG>"
                "-DROOT_KIND=${root_kind}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Scene/Process/SceneDocumentProcess.cmake")
        set_tests_properties(SceneDocumentProcess.${root_kind} PROPERTIES TIMEOUT 270 LABELS "scene;process")
    endforeach()
endif()
