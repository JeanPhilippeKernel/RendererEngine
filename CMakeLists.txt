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

option (BUILD_SANDBOX_PROJECTS "build example projects that show how to use core engine" OFF)

# Externals dependencies
#
add_subdirectory (${EXTERNAL_DIR}/fmt)
add_subdirectory (${EXTERNAL_DIR}/glad)
add_subdirectory (${EXTERNAL_DIR}/imgui)

add_subdirectory (${EXTERNAL_DIR}/glfw)

add_subdirectory (${EXTERNAL_DIR}/spdlog)
add_subdirectory (${EXTERNAL_DIR}/glm)
add_subdirectory (${EXTERNAL_DIR}/entt)
add_subdirectory (${EXTERNAL_DIR}/assimp)
add_subdirectory (${EXTERNAL_DIR}/stduuid)
add_subdirectory (${EXTERNAL_DIR}/yaml-cpp)

# Core engine lib is here
#
add_subdirectory(ZEngine)

# Editor is here
#
add_subdirectory(Tetragrama)
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT tetragrama)
endif ()

# Post build operation
#
set(SYSTEM_NAME ${CMAKE_SYSTEM_NAME})

add_custom_target(AssembleContent ALL
	COMMENT "Copying assets and resources contents"
	DEPENDS
		zEngineLib
		tetragrama
)

add_custom_command(TARGET AssembleContent
	POST_BUILD
	COMMAND pwsh ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/PostBuild.ps1 -Configurations $<IF:$<CONFIG:Debug>,Debug,Release>
)