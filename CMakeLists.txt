cmake_minimum_required (VERSION 3.17 FATAL_ERROR)

project (RendererEngine
	VERSION 1.0
	DESCRIPTION "Renderer Engine is an open-source 2D - 3D rendering engine written in C/C++"
	LANGUAGES CXX C
)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

# Core invariants
#

include(Scripts/CMake/NuGet.cmake)
include(Scripts/CMake/CppWinRT.cmake)

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

list( APPEND CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib )

# Externals dependencies
#
if (NOT LAUNCHER_ONLY)

    ## Setup Dependencies
    include(dependencies.cmake)

	# Core engine lib is here
	#
	add_subdirectory (ZEngine)

	# Editor is here
	#
	add_subdirectory (Tetragrama)

	# Entry point is here
	#
	add_subdirectory (Obelisk)
endif ()

# Launcher is here
#
add_subdirectory (Panzerfaust)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set_property (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Panzerfaust)
endif ()
