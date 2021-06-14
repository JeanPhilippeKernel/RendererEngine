include(${EXTERNAL_DIR}/dependencies.cmake)

file (GLOB_RECURSE HEADER_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../include/ZEngine/*.h)
file (GLOB CPP_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_library (zEngineLib
	STATIC
		${CPP_FILES_LIST}
		${HEADER_FILES_LIST}
)

target_include_directories (zEngineLib
	PUBLIC 
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	PRIVATE 
		../include/ZEngine
		../../__externals
)

target_compile_definitions (zEngineLib
	PRIVATE
		IMGUI_IMPL_OPENGL_LOADER_GLAD2
		Z_ENGINE_PLATFORM
)

target_link_libraries (zEngineLib
	PRIVATE
		imported::sdl2
		imported::fmt
		imported::glm
)

