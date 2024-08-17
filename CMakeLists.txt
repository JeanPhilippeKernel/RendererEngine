cmake_minimum_required (VERSION 3.17 FATAL_ERROR)

project (RendererEngine
	VERSION 1.0
	DESCRIPTION "Renderer Engine is an open-source 2D - 3D rendering engine written in C/C++"
	LANGUAGES CXX C
)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

# Core invariants
#
get_filename_component (ENLISTMENT_ROOT "." ABSOLUTE CACHE)
get_filename_component (EXTERNAL_DIR "${ENLISTMENT_ROOT}/__externals" ABSOLUTE CACHE)
get_filename_component (EXAMPLE_DIR "${ENLISTMENT_ROOT}/Examples" ABSOLUTE CACHE)

include(${ENLISTMENT_ROOT}/Scripts/CMake/NuGet.cmake)


option (COPY_EXAMPLE_PROJECT "Copy example projects that show how to use Launcher" ON)
option (LAUNCHER_ONLY "Build Launcher only" OFF)

set (MACOSX_ARCHITECTURE_ARM64 OFF)
if (APPLE)
    # Use uname to detect the macOS processor architecture
    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE MACOSX_ARCHITECTURE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
	if (MACOSX_ARCHITECTURE STREQUAL "arm64")
		set (MACOSX_ARCHITECTURE_ARM64 ON)
	endif ()
endif ()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING) # Fix Warning from VS 2022 +
endif()

# Externals dependencies
#
if (NOT LAUNCHER_ONLY)
	add_subdirectory (${EXTERNAL_DIR}/fmt)
	add_subdirectory (${EXTERNAL_DIR}/imgui)
	add_subdirectory (${EXTERNAL_DIR}/glfw)
	add_subdirectory (${EXTERNAL_DIR}/spdlog)
	add_subdirectory (${EXTERNAL_DIR}/glm)
	add_subdirectory (${EXTERNAL_DIR}/entt)
	add_subdirectory (${EXTERNAL_DIR}/assimp)
	add_subdirectory (${EXTERNAL_DIR}/stduuid)
	add_subdirectory (${EXTERNAL_DIR}/yaml-cpp)
	add_subdirectory (${EXTERNAL_DIR}/SPIRV-headers)
	add_subdirectory (${EXTERNAL_DIR}/SPIRV-Tools)
	add_subdirectory (${EXTERNAL_DIR}/glslang)
	add_subdirectory (${EXTERNAL_DIR}/SPIRV-Cross)
	add_subdirectory (${EXTERNAL_DIR}/gtest)
	add_subdirectory (${EXTERNAL_DIR}/VulkanMemoryAllocator)

	set (CMAKE_PREFIX_PATH
		${CMAKE_PREFIX_PATH}
		${EXTERNAL_DIR}/Vulkan-Headers/build/install/share/cmake
	)
	add_subdirectory (${EXTERNAL_DIR}/Vulkan-Loader)

	# Core engine lib is here
	#
	add_subdirectory (ZEngine)

	# Editor is here
	#
	add_subdirectory (Tetragrama)
endif ()

# Launcher is here
#
add_subdirectory (Panzerfaust)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set_property (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Panzerfaust)
endif ()

# Post build operation
#
add_custom_target (AssembleContent ALL
	COMMENT "Copying assets and resources contents"
	DEPENDS Panzerfaust
)

set (SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set (SYSTEM_ARCHITECTURE "win-x64")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set (SYSTEM_ARCHITECTURE "linux-x64")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	if (MACOSX_ARCHITECTURE_ARM64)
		set (SYSTEM_ARCHITECTURE "osx-arm64")
	else ()
		set (SYSTEM_ARCHITECTURE "osx-x64")
	endif ()
endif ()
add_custom_command (TARGET AssembleContent
	POST_BUILD
	COMMAND pwsh ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/PostBuild.ps1 -SystemName ${SYSTEM_NAME} -Architectures ${SYSTEM_ARCHITECTURE} -Configurations $<IF:$<CONFIG:Debug>,Debug,Release> $<$<BOOL:${LAUNCHER_ONLY}>:-LauncherOnly>
)

# Copying Examples dir
#
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Examples/projectConfig.json)
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/Examples/projectConfig.json DESTINATION ${CMAKE_BINARY_DIR}/Examples)
endif ()
