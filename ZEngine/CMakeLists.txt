cmake_minimum_required (VERSION 3.17)

project (ZEngine 
	VERSION 1.0
	DESCRIPTION "Z-Engine is the core engine lib that provides abstraction on top of OpenGL/DirectX/Metal"
	LANGUAGES CXX C
)

# Compiler verification
#
if ((${CMAKE_SYSTEM_NAME} STREQUAL "Linux") 
	AND (NOT ${CMAKE_CXX_COMPILER_VERSION} MATCHES "7.*.*"))
	message(FATAL_ERROR "CLANG compiler version should be 7.*.* !")
endif ()

add_subdirectory (src)