cmake_minimum_required (VERSION 3.17)

project (Tetragrama 
	VERSION 1.0
	DESCRIPTION "Tetragrama, the ZEngine Editor"
	LANGUAGES CXX
)

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

file (GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
file (GLOB_RECURSE CPP_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

source_group (TREE ${PROJECT_SOURCE_DIR} PREFIX "Source Files" FILES ${HEADER_FILES} ${CPP_FILES})

set (TARGET_NAME tetragrama)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_executable (${TARGET_NAME} WIN32)
else()
    add_executable (${TARGET_NAME})
endif()

target_sources(${TARGET_NAME} PUBLIC ${HEADER_FILES} ${CPP_FILES})

# We set this debugger directory to find assets and resources file
# after being copied to Debug and Release output directories 
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(${TARGET_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)$(Configuration)")
endif ()

target_include_directories (${TARGET_NAME}
	PRIVATE
        .
        ./Components
        ./Controllers
        ./Inputs
        ./Layers
        ./Messengers
        ./Helpers
        ./Importers
        ./Serializers
		${ENLISTMENT_ROOT}/ZEngine
)

target_precompile_headers(${TARGET_NAME} PRIVATE pch.h)
target_compile_definitions(${TARGET_NAME}
    PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
)

target_link_libraries(${TARGET_NAME} PRIVATE zEngineLib)

set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "zEngineEditor")