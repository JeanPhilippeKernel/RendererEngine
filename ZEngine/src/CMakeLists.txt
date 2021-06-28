include(${EXTERNAL_DIR}/external.cmake)
include(${EXTERNAL_DIR}/dependencies.cmake)

file (GLOB_RECURSE HEADER_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/ZEngine/*.h)
file (GLOB CPP_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

source_group (TREE ${PROJECT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${HEADER_FILES_LIST})
source_group (TREE ${PROJECT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${CPP_FILES_LIST})

add_library (zEngineLib
	STATIC
		${HEADER_FILES_LIST}
		${CPP_FILES_LIST}
)

target_include_directories (zEngineLib
	PUBLIC 
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	PUBLIC 
		../include/ZEngine
		${EXTERNAL_DIR}
		${EXTERNAL_DIR}/imgui/src
)

target_compile_definitions (zEngineLib
	PUBLIC
		Z_ENGINE_PLATFORM
)

target_link_libraries (zEngineLib
	PUBLIC
		imported::sdl2
		imported::fmt
		imported::glm
		OpenGL::GL
		glad
		imgui
)

