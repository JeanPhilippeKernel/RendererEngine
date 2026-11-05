file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    Memory/*.cpp
    Containers/*.cpp
    Cook/*.cpp
    Importers/*.cpp
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
target_sources(ZEngineTests PRIVATE Cook/Process/SceneCookCustomSchema.cpp)

# Use the reviewed contract bytes themselves, also in installed/headless tests.
set(COOK_MANIFEST_GOLDEN_PATH "${CMAKE_SOURCE_DIR}/docs/design/future-plan/scene-cooker-contracts/manifest-v1.golden.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${COOK_MANIFEST_GOLDEN_PATH}")
file(READ "${COOK_MANIFEST_GOLDEN_PATH}" COOK_MANIFEST_GOLDEN)
set(COOK_MANIFEST_NEGATIVE_PATH "${CMAKE_SOURCE_DIR}/docs/design/future-plan/scene-cooker-contracts/manifest-v1.negative-cases.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${COOK_MANIFEST_NEGATIVE_PATH}")
file(READ "${COOK_MANIFEST_NEGATIVE_PATH}" COOK_MANIFEST_NEGATIVE)
configure_file(Cook/CookManifestGolden.h.in "generated/CookManifestGolden.h" @ONLY)
set_property(SOURCE Cook/CookManifestWriter_test.cpp Cook/CookManifestReader_test.cpp APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated")

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
set(MATERIAL_ALPHA_SOURCE "${CMAKE_SOURCE_DIR}/Resources/Shaders/material_alpha.glsl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MATERIAL_ALPHA_SOURCE}")
file(READ "${MATERIAL_ALPHA_SOURCE}" MATERIAL_ALPHA_SOURCE)
configure_file(Rendering/MaterialAlphaShaderSource.h.in "generated/MaterialAlphaShaderSource.h" @ONLY)
set_property(SOURCE Rendering/GpuAllocatorTest.cpp APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(ZEngineTests PRIVATE
    zEngineLib
    GTest::gtest_main
)

add_executable(SceneDocumentProcess Scene/Process/SceneDocumentProcess.cpp)
target_link_libraries(SceneDocumentProcess PRIVATE zEngineLib GTest::gtest)
add_executable(SceneCookProcess Cook/Process/SceneCookProcess.cpp Cook/Process/SceneCookProjectSmoke.cpp Cook/Process/SceneCookCustomSchema.cpp)
target_link_libraries(SceneCookProcess PRIVATE zEngineLib GTest::gtest)
add_executable(SceneCookResourceProcess Cook/Process/SceneCookResourceProcess.cpp Cook/Process/SceneCookAssetResourceProcess.cpp Cook/Process/SceneCookSceneResourceProcess.cpp)
target_link_libraries(SceneCookResourceProcess PRIVATE zEngineLib GTest::gtest)
add_executable(YAMLAllocationProcess Scene/Process/YAMLAllocationProcess.cpp)
target_link_libraries(YAMLAllocationProcess PRIVATE yaml)
target_include_directories(YAMLAllocationProcess PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(YAMLAllocationProcess PRIVATE cxx_std_20)

set(ZENGINE_TEST_EXECUTABLES ZEngineTests SceneDocumentProcess SceneCookProcess SceneCookResourceProcess YAMLAllocationProcess)
foreach(test_target IN LISTS ZENGINE_TEST_EXECUTABLES)
    if(APPLE)
        # @loader_path/../lib resolves to lib/ relative to wherever the binary lands,
        # surviving the artifact prefix-stripping that makes absolute CMAKE_INSTALL_PREFIX
        # paths wrong on the CI runner after download.
        set_target_properties(${test_target} PROPERTIES INSTALL_RPATH "@loader_path/../lib")
    else()
        set_target_properties(${test_target} PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)
    endif()
endforeach()

install(TARGETS ${ZENGINE_TEST_EXECUTABLES}
    RUNTIME DESTINATION tests)
foreach(test_target IN LISTS ZENGINE_TEST_EXECUTABLES)
    zengine_sign_installed_target(${test_target} tests)
endforeach()
install(FILES Scene/Process/SceneDocumentProcess.cmake DESTINATION tests)
install(FILES Cook/Process/SceneCookProcess.cmake DESTINATION tests)
install(FILES Cook/Process/SceneCookDiskFullProcess.cmake DESTINATION tests)
install(FILES Cook/Process/SceneCookStressProcess.cmake DESTINATION tests)
install(DIRECTORY Cook/Fixtures/ DESTINATION tests/cook-fixtures)

if(APPLE)
    # Install freetype.framework alongside vulkan in lib/ so the @loader_path/../lib
    # rpath entry resolves @rpath/freetype.framework/... correctly on CI.
    install(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/freetype-build/$<CONFIG>/freetype.framework"
        DESTINATION lib
        USE_SOURCE_PERMISSIONS)
endif()

if(APPLE)
    foreach(test_target IN LISTS ZENGINE_TEST_EXECUTABLES)
        add_custom_command(TARGET ${test_target} POST_BUILD
            COMMAND codesign --force --sign - $<TARGET_FILE:${test_target}>
            COMMENT "Ad-hoc signing ${test_target}"
        )
    endforeach()
endif()

if(BUILD_TESTING)
    include(GoogleTest)
    gtest_discover_tests(ZEngineTests)
    add_test(NAME YAMLAllocationProcess COMMAND YAMLAllocationProcess)
    set_tests_properties(YAMLAllocationProcess PROPERTIES TIMEOUT 30 LABELS "scene;process;memory")
    # Actual image/material/mesh/environment/scene limits belong in one serialized
    # process, not every unit shard. Mappings drain; no editor/GPU needed.
    add_test(NAME SceneCookResourceProcess COMMAND SceneCookResourceProcess)
    set_tests_properties(SceneCookResourceProcess PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE LABELS "cook;process;memory;resource")
    add_test(NAME SceneCookStressProcess
        COMMAND ${CMAKE_COMMAND}
            "-DHELPER=$<TARGET_FILE:SceneCookProcess>"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cook-stress-$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Cook/Process/SceneCookStressProcess.cmake")
    set_tests_properties(SceneCookStressProcess PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE LABELS "cook;process;memory;resource;scene")
    foreach(root_kind plain unicode)
        add_test(NAME SceneDocumentProcess.${root_kind}
            COMMAND ${CMAKE_COMMAND}
                "-DHELPER=$<TARGET_FILE:SceneDocumentProcess>"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/scene-process-$<CONFIG>"
                "-DROOT_KIND=${root_kind}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Scene/Process/SceneDocumentProcess.cmake")
        set_tests_properties(SceneDocumentProcess.${root_kind} PROPERTIES TIMEOUT 270 LABELS "scene;process")
        add_test(NAME SceneCookProcess.${root_kind}
            COMMAND ${CMAKE_COMMAND}
                "-DHELPER=$<TARGET_FILE:SceneCookProcess>"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cook-process-$<CONFIG>"
                "-DGOLDEN_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/Cook/Fixtures"
                "-DROOT_KIND=${root_kind}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/Cook/Process/SceneCookProcess.cmake")
        set_tests_properties(SceneCookProcess.${root_kind} PROPERTIES TIMEOUT 180 LABELS "cook;scene;process")
    endforeach()
endif()
