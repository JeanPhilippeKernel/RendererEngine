include(${EXTERNAL_DIR}/externals.cmake)

file (GLOB_RECURSE HEADER_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/ZEngine/*.h)
file (GLOB CPP_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file (GLOB_RECURSE RESOURCE_FILES_LIST CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/../Resources/*.glsl)

source_group (TREE ${PROJECT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${HEADER_FILES_LIST})
source_group (TREE ${PROJECT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${CPP_FILES_LIST})
source_group (TREE ${PROJECT_SOURCE_DIR}/../Resources PREFIX "Resources Files" FILES ${RESOURCE_FILES_LIST})

# ZEngine source files
#
list (APPEND Sources ${CPP_FILES_LIST})

if ((APPLE) OR (${CMAKE_SYSTEM_NAME} STREQUAL "Windows"))
	list (APPEND Sources 
			GlfwWindow/OpenGLContext.cpp
			GlfwWindow/OpenGLWindow.cpp
	)
else ()
	list (APPEND Sources 
			SDL2Window/OpenGLContext.cpp
			SDL2Window/OpenGLWindow.cpp
	)
endif ()

add_library (zEngineLib
	STATIC
		${HEADER_FILES_LIST}
		${Sources}
		${EXTERNAL_DIR}/ImGuizmo/ImGuizmo.h
		${EXTERNAL_DIR}/ImGuizmo/ImGuizmo.cpp
		${RESOURCE_FILES_LIST}
)

target_include_directories (zEngineLib
	PUBLIC 
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_SOURCE_DIR}/include/ZEngine
		${EXTERNAL_DIR}
		${EXTERNAL_INCLUDE_DIRS}
)

target_precompile_headers(zEngineLib PRIVATE ${PROJECT_SOURCE_DIR}/include/ZEngine/pch.h)

target_compile_definitions (zEngineLib
	PUBLIC
		ZENGINE_PLATFORM
		YAML_CPP_STATIC_DEFINE
)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(zEngineLib PRIVATE stdc++fs)
endif ()

if (USE_SDL_LIBS)
	target_link_libraries (zEngineLib PUBLIC imported::sdl2)
else ()
	target_link_libraries (zEngineLib PUBLIC imported::glfw)
endif ()

target_link_libraries (zEngineLib
	PUBLIC
		imported::fmt
		imported::spdlog
		imported::glad
		imported::glm
		imported::imgui
		imported::entt
		imported::assimp
		imported::stduuid
		imported::yaml-cpp
		OpenGL::GL
)
