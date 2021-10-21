include (toolchain.cmake)

cmake_minimum_required (VERSION 3.17 FATAL_ERROR)

project (RendererEngine 
	VERSION 1.0
	DESCRIPTION "Renderer Engine is an open-source 2D - 3D rendering engine written in C/C++"
	LANGUAGES CXX C
)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

set (CMAKE_CXX_STANDARD_REQUIRED ON) 
set	(CMAKE_CXX_STANDARD 17)

set (CMAKE_C_STANDARD_REQUIRED ON)
set (CMAKE_C_STANDARD 11)

# Core invariants
#
get_filename_component (ENLISTMENT_ROOT "." ABSOLUTE CACHE)
get_filename_component (EXTERNAL_DIR "${ENLISTMENT_ROOT}/__externals" ABSOLUTE CACHE)
get_filename_component (EXAMPLE_DIR "${ENLISTMENT_ROOT}/Examples" ABSOLUTE CACHE)

option (BUILD_SANDBOX_PROJECTS "build example projects that show how to use core engine" ON)

# Externals dependencies
#
add_subdirectory (${EXTERNAL_DIR}/fmt)
add_subdirectory (${EXTERNAL_DIR}/glad)
add_subdirectory (${EXTERNAL_DIR}/imgui)
add_subdirectory (${EXTERNAL_DIR}/SDL2)
add_subdirectory (${EXTERNAL_DIR}/spdlog)
add_subdirectory (${EXTERNAL_DIR}/glm)

# Core engine lib is here
#
add_subdirectory(ZEngine)

# Examples
#
if (BUILD_SANDBOX_PROJECTS)
	add_subdirectory(${EXAMPLE_DIR}/Sandbox)
	add_subdirectory(${EXAMPLE_DIR}/Sandbox3D)
endif ()

# Post build operation
#
set(SYSTEM_NAME ${CMAKE_SYSTEM_NAME})

add_custom_target(AssembleContent ALL
	COMMENT "Copying assets and resources contents"
	DEPENDS 
		zEngineLib
		sandbox2d 
		sandbox3d 
		SDL2main 
)

add_custom_command(TARGET AssembleContent
	POST_BUILD
	COMMAND pwsh ${CMAKE_CURRENT_SOURCE_DIR}/Scripts/PostBuild.ps1 -SystemNames ${SYSTEM_NAME} -Configurations $<IF:$<CONFIG:Debug>,Debug,Release>
)