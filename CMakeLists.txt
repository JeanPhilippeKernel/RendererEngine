cmake_minimum_required (VERSION 3.17)

project (RendererEngine 
	VERSION 1.0
	DESCRIPTION "Renderer Engine is an open-source 2D - 3D rendering engine written in C/C++"
	LANGUAGES CXX
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Core invariants
get_filename_component (ENLISTMENT_ROOT "." ABSOLUTE CACHE)
get_filename_component (EXTERNAL_DIR "${ENLISTMENT_ROOT}/__externals" ABSOLUTE CACHE)

option (BUILD_SANDBOX "build example projects that show how to use core engine" ON)
# Externals dependencies
add_subdirectory (${EXTERNAL_DIR}/fmt)

add_subdirectory (${EXTERNAL_DIR}/SDL2)

set (BUILD_STATIC_LIBS ON)
add_subdirectory (${EXTERNAL_DIR}/glm)

# core engine lib is here
add_subdirectory(ZEngine)

# examples
if (BUILD_SANDBOX)
	add_subdirectory(Sandbox)
	add_subdirectory(Sandbox3D)
endif ()