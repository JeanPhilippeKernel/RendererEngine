cmake_minimum_required (VERSION 3.17)

project (ZEngine 
	VERSION 1.0
	DESCRIPTION "ZEngine is the core engine lib that provides abstraction on top of OpenGL/DirectX/Metal"
	LANGUAGES CXX C
)

# Compiler verification in Linux
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	if ((NOT ${CMAKE_CXX_COMPILER_VERSION} MATCHES "11.*.*") AND (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang"))
		message(FATAL_ERROR "CLANG compiler version should be 11.*.* !")
	elseif ((NOT ${CMAKE_CXX_COMPILER_VERSION} MATCHES "11.*.*") AND (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU"))
		message(FATAL_ERROR "GNU compiler version should be 11.*.* !")
	endif ()
endif ()

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	add_compile_definitions(_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING) # Fix Warning from VS 2022 +
endif()


add_subdirectory (src)
add_subdirectory(tests)