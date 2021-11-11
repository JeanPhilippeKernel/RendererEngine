include(${EXTERNAL_DIR}/externals.cmake)

file (GLOB_RECURSE HEADER_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/ZEngine/*.h)
file (GLOB CPP_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file (GLOB_RECURSE RESOURCE_FILES_LIST CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/../Resources/*.glsl)

source_group (TREE ${PROJECT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${HEADER_FILES_LIST})
source_group (TREE ${PROJECT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${CPP_FILES_LIST})
source_group (TREE ${PROJECT_SOURCE_DIR}/../Resources PREFIX "Resources Files" FILES ${RESOURCE_FILES_LIST})

add_library (zEngineLib
	STATIC
		${HEADER_FILES_LIST}
		${CPP_FILES_LIST}
		${RESOURCE_FILES_LIST}
)

target_include_directories (zEngineLib
	PUBLIC 
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/include/ZEngine
		${EXTERNAL_DIR}
		${EXTERNAL_INCLUDE_DIRS}
)

target_compile_definitions (zEngineLib
	PUBLIC
		Z_ENGINE_PLATFORM
)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(zEngineLib PRIVATE stdc++fs)
endif ()

target_link_libraries (zEngineLib
	PUBLIC
		imported::fmt
		imported::spdlog
		imported::glad
		imported::glm
		imported::imgui
		imported::sdl2
		imported::glfw
		OpenGL::GL
)
