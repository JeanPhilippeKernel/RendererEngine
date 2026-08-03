cmake_minimum_required (VERSION 3.23)

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

add_library (Tetragrama)

target_sources(Tetragrama 
    PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR}/..
    PRIVATE ${CPP_FILES}
)

# We set this debugger directory to find assets and resources file
# after being copied to Debug and Release output directories 
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(Tetragrama PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)$(Configuration)")
endif ()

target_compile_definitions(Tetragrama
    PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
)

target_link_libraries(Tetragrama PUBLIC zEngineLib)
