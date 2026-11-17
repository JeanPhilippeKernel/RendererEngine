add_executable(ZEngineRuntime)
target_sources(ZEngineRuntime PRIVATE EntryPoint.cpp)
target_link_libraries(ZEngineRuntime PRIVATE zEngineLib CLI11::CLI11)
target_compile_definitions(ZEngineRuntime PRIVATE NOMINMAX UNICODE _UNICODE)
# Use an explicit per-configuration output directory so asset copying can run
# before the executable without a target-file dependency cycle.
if(CMAKE_CONFIGURATION_TYPES)
    set(_runtime_output_dir "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
else()
    set(_runtime_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
endif()
set_target_properties(ZEngineRuntime PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_runtime_output_dir}")
add_custom_target(ZEngineRuntimeAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Resources/Editor/Settings" "${_runtime_output_dir}/ZodiacEngine/Settings"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Resources/Shaders/Cache" "${_runtime_output_dir}/ZodiacEngine/Shaders/Cache"
    DEPENDS CompileShaders
    VERBATIM)
add_dependencies(ZEngineRuntime ZEngineRuntimeAssets)

if(APPLE)
    set_target_properties(ZEngineRuntime PROPERTIES INSTALL_RPATH "@loader_path/../lib;@loader_path/../Library/Frameworks")
elseif(UNIX)
    set_target_properties(ZEngineRuntime PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
install(TARGETS ZEngineRuntime DESTINATION bin)
zengine_sign_installed_target(ZEngineRuntime bin)

if(BUILD_TESTING)
    install(FILES tests/RuntimeCli.cmake DESTINATION tests)
    add_test(NAME RuntimeCLI.StartupArguments
        COMMAND ${CMAKE_COMMAND} "-DRUNTIME_EXECUTABLE=$<TARGET_FILE:ZEngineRuntime>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RuntimeCli.cmake")
endif()

option(ZENGINE_ENABLE_RUNTIME_DESKTOP_TESTS "Register editor-free runtime Vulkan smoke tests" OFF)
if(BUILD_TESTING AND ZENGINE_ENABLE_RUNTIME_DESKTOP_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME RuntimeDesktop.CookedStartupAndFailures
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/scene/cooked_scene_smoke.py"
            --cooker "$<TARGET_FILE:SceneCookProcess>" --obelisk "$<TARGET_FILE:ZEngineRuntime>"
            --standalone --runtime-camera main --failure-cases --audit-source --require-validation)
    set_tests_properties(RuntimeDesktop.CookedStartupAndFailures PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE LABELS "runtime;scene;desktop")
endif()
