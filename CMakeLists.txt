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


if(NOT LAUNCHER_ONLY)
	# Externals dependencies
	#
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
	add_subdirectory(ZEngine)

	# Editor is here
	#
	add_subdirectory(Tetragrama)
endif ()
# Launcher is here
#
add_subdirectory(Panzerfaust)


if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Panzerfaust)
endif ()


# Post build operation
#
set(SYSTEM_NAME ${CMAKE_SYSTEM_NAME})

if(NOT LAUNCHER_ONLY)
	add_custom_target(BuildLauncher ALL
	COMMENT "Building Panzerfaust"
	DEPENDS
		zEngineLib
		tetragrama
	)
else ()
	add_custom_target(BuildLauncher ALL
	COMMENT "Building Panzerfaust"
	)
endif ()

add_custom_command(TARGET BuildLauncher
	POST_BUILD
	COMMAND dotnet build ${CMAKE_CURRENT_SOURCE_DIR}/Panzerfaust/Panzerfaust.csproj -c $<IF:$<CONFIG:Debug>,Debug,Release> -v n
)

add_custom_target(AssembleContent ALL
	COMMENT "Copying assets and resources contents"
	DEPENDS
		BuildLauncher
)

if(NOT LAUNCHER_ONLY)
	add_custom_command(TARGET AssembleContent
		POST_BUILD
		COMMAND pwsh ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/PostBuild.ps1 -Configurations $<IF:$<CONFIG:Debug>,Debug,Release>
	)
else ()
	add_custom_command(TARGET AssembleContent
	POST_BUILD
	COMMAND pwsh ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/PostBuild.ps1 -Configurations $<IF:$<CONFIG:Debug>,Debug,Release> -LauncherOnly
	)
endif ()

# Copying Examples dir
#
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Examples/projectConfig.json)
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/Examples/projectConfig.json DESTINATION ${CMAKE_BINARY_DIR}/Examples)
endif()


