cmake_minimum_required (VERSION 3.17 FATAL_ERROR)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" ZENGINE_VERSION)
string(STRIP "${ZENGINE_VERSION}" ZENGINE_VERSION)

project (RendererEngine
	VERSION ${ZENGINE_VERSION}
	DESCRIPTION "Renderer Engine is an open-source 2D - 3D rendering engine written in C/C++"
	LANGUAGES CXX C
)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

# Install git hooks automatically at configure time so contributors don't need
# to run install-hooks.sh manually.
#
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    file(GLOB _hook_files "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/hooks/*")
    foreach(_hook ${_hook_files})
        get_filename_component(_hook_name "${_hook}" NAME)
        set(_dst "${CMAKE_CURRENT_SOURCE_DIR}/.git/hooks/${_hook_name}")
        file(COPY "${_hook}" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/.git/hooks")
        file(CHMOD "${_dst}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        message(STATUS "Installed git hook: ${_hook_name}")
    endforeach()
endif()

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

    # Enable CMake's CTest Functionality
    #
    include(CTest)

    # Setup Dependencies
    #
    include(dependencies.cmake)

    # Compile Shaders
    #
    add_subdirectory (Resources)

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
