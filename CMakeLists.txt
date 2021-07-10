# Selecting Compiler 
#
if (UNIX)
	set (CMAKE_CXX_COMPILER "/usr/bin/clang++")
	set (CMAKE_C_COMPILER 	"/usr/bin/clang")
endif ()

cmake_minimum_required (VERSION 3.17 FATAL_ERROR)

project (RendererEngine 
	VERSION 1.0
	DESCRIPTION "Renderer Engine is an open-source 2D - 3D rendering engine written in C/C++"
	LANGUAGES CXX C
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set (CMAKE_CXX_STANDARD_REQUIRED ON) 

if (MSVC)
	set(CMAKE_CXX_STANDARD 17)
else ()
	set(CMAKE_CXX_STANDARD 20)
endif ()

set(CMAKE_C_STANDARD 11)

# Core invariants
get_filename_component (ENLISTMENT_ROOT "." ABSOLUTE CACHE)
get_filename_component (EXTERNAL_DIR "${ENLISTMENT_ROOT}/__externals" ABSOLUTE CACHE)
get_filename_component (OUTPUT_BUILD_DIR "${ENLISTMENT_ROOT}/out/build" ABSOLUTE CACHE)
get_filename_component (EXAMPLE_DIR "${ENLISTMENT_ROOT}/Examples" ABSOLUTE CACHE)

option (BUILD_SANDBOX "build example projects that show how to use core engine" ON)

# Externals dependencies
add_subdirectory (${EXTERNAL_DIR}/fmt)
add_subdirectory (${EXTERNAL_DIR}/glad)
add_subdirectory (${EXTERNAL_DIR}/imgui)
add_subdirectory (${EXTERNAL_DIR}/SDL2)
add_subdirectory (${EXTERNAL_DIR}/spdlog)


set (BUILD_STATIC_LIBS ON)
add_subdirectory (${EXTERNAL_DIR}/glm)

# core engine lib is here
add_subdirectory(ZEngine)

# examples
if (BUILD_SANDBOX)
	add_subdirectory(${EXAMPLE_DIR}/Sandbox)
	add_subdirectory(${EXAMPLE_DIR}/Sandbox3D)
endif ()