cmake_minimum_required (VERSION 3.17)

project (Obelisk 
	VERSION 1.0
	DESCRIPTION "Obelisk, The game application entry point"
	LANGUAGES CXX
)

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

set (TARGET_NAME Obelisk)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_executable (${TARGET_NAME} WIN32)
else()
    add_executable (${TARGET_NAME})
endif()

target_sources(${TARGET_NAME} PUBLIC EntryPoint.cpp)

# We set this debugger directory to find assets and resources file
# after being copied to Debug and Release output directories 
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(${TARGET_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)$(Configuration)")
endif ()

include(${EXTERNAL_DIR}/externals.cmake)

target_include_directories (${TARGET_NAME}
    PRIVATE
        .
        ${ENLISTMENT_ROOT}/ZEngine
        ${ENLISTMENT_ROOT}/Tetragrama
)

target_precompile_headers(${TARGET_NAME} PRIVATE pch.h)

target_link_libraries(${TARGET_NAME} PRIVATE
    zEngineLib
    tetragrama
    imported::External_obeliskLibs
)