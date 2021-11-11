include (${ENLISTMENT_ROOT}/dependencies.cmake)

file (GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
file (GLOB_RECURSE CPP_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

source_group (TREE ${PROJECT_SOURCE_DIR}/src PREFIX "Header Files" FILES ${HEADER_FILES})
source_group (TREE ${PROJECT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${CPP_FILES})

set (TARGET_NAME tetragrama)
add_executable (${TARGET_NAME} 
    ${HEADER_FILES}
    ${CPP_FILES}
)

# We set this debugger directory to find assets and resources file
# after being copied to Debug and Release output directories 
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(${TARGET_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)$(Configuration)")
endif ()

target_include_directories (${TARGET_NAME}
	PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/Components
        ${CMAKE_CURRENT_SOURCE_DIR}/Layers
		${ENLISTMENT_ROOT}/ZEngine/include
)

target_precompile_headers(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

target_compile_definitions (${TARGET_NAME}
	PRIVATE
		IMGUI_IMPL_OPENGL_LOADER_GLAD
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        imported::zEngineLib
)

set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "zEngineEditor")