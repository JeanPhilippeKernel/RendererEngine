cmake_minimum_required (VERSION 3.23)

project (Obelisk 
	VERSION 1.0
	DESCRIPTION "Obelisk, The game application entry point"
	LANGUAGES CXX
)

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

set (TARGET_NAME Obelisk)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_executable (${TARGET_NAME} WIN32)
else()
    add_executable (${TARGET_NAME})
endif()

target_sources(${TARGET_NAME} PUBLIC EntryPoint.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_DIR})

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    enable_language(RC)
    set(OBELISK_ICO_PATH
        "${CMAKE_SOURCE_DIR}/Resources/Editor/Settings/Icons/AppIconBadge.ico")
    configure_file(Obelisk.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Obelisk.rc" @ONLY)
    target_sources(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Obelisk.rc")
endif()

# We set this debugger directory to find assets and resources file
# after being copied to Debug and Release output directories 
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(${TARGET_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)$(Configuration)")
endif ()


target_compile_definitions(${TARGET_NAME}
    PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
)

target_link_libraries(${TARGET_NAME} PUBLIC
    Tetragrama
    imported::External_obeliskLibs
)

# The executable loads packaged SPIR-V at startup, so an explicit Obelisk build
# must refresh the shader package before the editor is run.
add_dependencies(${TARGET_NAME} CompileShaders)
if (TARGET CopyEngineSettings)
    add_dependencies(${TARGET_NAME} CopyEngineSettings)
endif()

if(APPLE)
    set_target_properties(${TARGET_NAME} PROPERTIES INSTALL_RPATH "@loader_path/../lib")
else()
    set_target_properties(${TARGET_NAME} PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)
endif()

install(TARGETS ${TARGET_NAME} DESTINATION bin)
zengine_sign_installed_target(${TARGET_NAME} bin)

# These tests create real windows and submit Vulkan work. Keep them opt-in so
# headless builders cannot silently substitute a non-rendering test for them.
option(ZENGINE_ENABLE_COOKED_DESKTOP_TESTS "Register cooked scene desktop Vulkan process tests" OFF)
if(BUILD_TESTING AND ZENGINE_ENABLE_COOKED_DESKTOP_TESTS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_cooked_smoke "${CMAKE_SOURCE_DIR}/Scripts/cooked_scene_smoke.py")
    set(_cooked_common "${Python3_EXECUTABLE}" "${_cooked_smoke}"
        --cooker "$<TARGET_FILE:SceneCookProcess>" --obelisk "$<TARGET_FILE:Obelisk>")
    add_test(NAME CookedDesktop.StartupAndPackageFailures COMMAND ${_cooked_common} --failure-cases)
    add_test(NAME CookedDesktop.Replacements COMMAND ${_cooked_common} --reloads 3)
    add_test(NAME CookedDesktop.RuntimeMainCamera COMMAND ${_cooked_common} --runtime-camera main)
    add_test(NAME CookedDesktop.RuntimeExplicitCamera COMMAND ${_cooked_common} --runtime-camera a0000000-0000-0000-0000-00000000000a)
    add_test(NAME CookedDesktop.StandaloneMainCamera COMMAND ${_cooked_common} --standalone --runtime-camera main)
    add_test(NAME CookedDesktop.StandaloneCapture COMMAND ${_cooked_common} --standalone --runtime-camera main --verify-capture)
    add_test(NAME CookedDesktop.StandaloneSourceIndependence COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 3 --audit-source)
    add_test(NAME CookedDesktop.StandaloneExplicitCamera COMMAND ${_cooked_common} --standalone --runtime-camera a0000000-0000-0000-0000-00000000000a)
    add_test(NAME CookedDesktop.StandaloneMissingCamera COMMAND ${_cooked_common} --standalone --runtime-camera b0000000-0000-0000-0000-00000000000b --reject-runtime-camera)
    add_test(NAME CookedDesktop.StandaloneReplacements COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 3)
    add_test(NAME CookedDesktop.StandaloneLifecycleStressSmoke COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 6 --lifecycle-stress)
    add_test(NAME CookedDesktop.StandaloneSteadyAndReplacement COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --steady-frames 30)
    add_test(NAME CookedDesktop.StandaloneMissingReplacementRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --missing-replacement --retry-after-failure)
    add_test(NAME CookedDesktop.StandaloneMissingReplacementCameraRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --missing-replacement-camera --retry-after-failure)
    add_test(NAME CookedDesktop.StandaloneCorruptReplacementMaterialRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --corrupt-replacement-material --retry-after-failure --require-validation --audit-source)
    add_test(NAME CookedDesktop.StandaloneMissingReplacementMaterialRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --missing-replacement-material --retry-after-failure --require-validation --audit-source)
    add_test(NAME CookedDesktop.StandaloneUploadCancellationRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --cancel-during-upload --retry-after-failure --require-validation --audit-source)
    add_test(NAME CookedDesktop.StandaloneCpuCancellationRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --cancel-during-cpu --retry-after-failure --require-validation --audit-source)
    add_test(NAME CookedDesktop.StandaloneUploadSubmissionRejectionRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --reject-submit --retry-after-failure --require-validation --audit-source)
    add_test(NAME CookedDesktop.StandaloneStagingRefusalRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --fail-staging --retry-after-failure)
    add_test(NAME CookedDesktop.StandalonePreflightRefusalRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --fail-preflight --retry-after-failure --require-validation --audit-source)
    add_test(NAME CookedDesktop.StandaloneDeviceLoss COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --device-loss)
    add_test(NAME CookedDesktop.StandaloneShutdownCpu COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --shutdown-cpu --require-validation)
    add_test(NAME CookedDesktop.StandaloneShutdownGpu COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --shutdown-gpu --require-validation)
    add_test(NAME CookedDesktop.StandaloneCorruptReplacementRetry COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --corrupt-replacement --retry-after-failure)
    add_test(NAME CookedDesktop.StandaloneSupersedeCpu COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --supersede-during-cpu)
    add_test(NAME CookedDesktop.StandaloneSupersedeWaiting COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --supersede-reload)
    add_test(NAME CookedDesktop.StandaloneSupersedeUpload COMMAND ${_cooked_common} --standalone --runtime-camera main --reloads 1 --supersede-during-upload)
    add_test(NAME CookedDesktop.RuntimeMissingCamera COMMAND ${_cooked_common} --runtime-camera b0000000-0000-0000-0000-00000000000b --reject-runtime-camera)
    add_test(NAME CookedDesktop.SupersedeWaiting COMMAND ${_cooked_common} --reloads 1 --supersede-reload)
    add_test(NAME CookedDesktop.SupersedeCpu COMMAND ${_cooked_common} --reloads 1 --supersede-during-cpu)
    add_test(NAME CookedDesktop.SupersedeUpload COMMAND ${_cooked_common} --reloads 1 --supersede-during-upload)
    add_test(NAME CookedDesktop.CpuCancellationRetry COMMAND ${_cooked_common} --reloads 1 --cancel-during-cpu --retry-after-failure)
    add_test(NAME CookedDesktop.UploadCancellationRetry COMMAND ${_cooked_common} --reloads 1 --cancel-during-upload --retry-after-failure)
    add_test(NAME CookedDesktop.StagingRefusalRetry COMMAND ${_cooked_common} --reloads 1 --fail-staging --retry-after-failure)
    add_test(NAME CookedDesktop.UploadSubmissionRejectionRetry COMMAND ${_cooked_common} --reloads 1 --reject-submit --retry-after-failure)
    add_test(NAME CookedDesktop.CorruptReplacementRetry COMMAND ${_cooked_common} --reloads 1 --corrupt-replacement --retry-after-failure)
    set_tests_properties(CookedDesktop.StartupAndPackageFailures CookedDesktop.Replacements CookedDesktop.RuntimeMainCamera CookedDesktop.RuntimeExplicitCamera CookedDesktop.StandaloneMainCamera CookedDesktop.StandaloneCapture CookedDesktop.StandaloneSourceIndependence CookedDesktop.StandaloneExplicitCamera CookedDesktop.StandaloneMissingCamera CookedDesktop.StandaloneReplacements CookedDesktop.StandaloneSteadyAndReplacement CookedDesktop.StandaloneMissingReplacementRetry CookedDesktop.StandaloneUploadCancellationRetry CookedDesktop.StandaloneCpuCancellationRetry CookedDesktop.StandaloneUploadSubmissionRejectionRetry CookedDesktop.StandaloneStagingRefusalRetry CookedDesktop.StandaloneDeviceLoss CookedDesktop.StandaloneCorruptReplacementRetry CookedDesktop.StandaloneSupersedeWaiting CookedDesktop.StandaloneSupersedeCpu CookedDesktop.StandaloneSupersedeUpload CookedDesktop.RuntimeMissingCamera CookedDesktop.SupersedeWaiting CookedDesktop.SupersedeCpu CookedDesktop.SupersedeUpload
        CookedDesktop.StandaloneLifecycleStressSmoke CookedDesktop.StandaloneShutdownCpu CookedDesktop.StandaloneShutdownGpu CookedDesktop.StandalonePreflightRefusalRetry CookedDesktop.StandaloneMissingReplacementCameraRetry CookedDesktop.StandaloneCorruptReplacementMaterialRetry CookedDesktop.StandaloneMissingReplacementMaterialRetry CookedDesktop.CpuCancellationRetry CookedDesktop.UploadCancellationRetry
        CookedDesktop.StagingRefusalRetry CookedDesktop.UploadSubmissionRejectionRetry CookedDesktop.CorruptReplacementRetry
        PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE LABELS "cook;scene;desktop;vulkan")
endif()
