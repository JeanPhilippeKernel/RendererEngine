# SDL2
add_library (imported::sdl2 INTERFACE IMPORTED)
set (SDL2_INCLUDE_PATH ${EXTERNAL_DIR}/SDL2)
target_include_directories (imported::sdl2 
	INTERFACE
		${SDL2_INCLUDE_PATH}/include
)
if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
	target_link_libraries (imported::sdl2 
		INTERFACE
			debug ${ENLISTMENT_ROOT}/out/build/__externals/SDL2/Debug/SDL2d.lib
	)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	target_link_libraries (imported::sdl2 
		INTERFACE
			debug ${ENLISTMENT_ROOT}/out/build/__externals/SDL2/libSDL2.a
	)
endif ()

# fmt
add_library (imported::fmt INTERFACE IMPORTED)
set (FMT_INCLUDE_PATH ${EXTERNAL_DIR}/fmt)
target_include_directories (imported::fmt 
	INTERFACE
		${FMT_INCLUDE_PATH}/include
)
if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
	target_link_libraries (imported::fmt 
		INTERFACE
			debug ${ENLISTMENT_ROOT}/out/build/__externals/fmt/Debug/fmtd.lib
	)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	target_link_libraries (imported::fmt 
		INTERFACE
			debug ${ENLISTMENT_ROOT}/out/build/__externals/fmt/libfmtd.a
	)
endif ()

# glm
add_library (imported::glm INTERFACE IMPORTED)
set (GLM_INCLUDE_PATH ${EXTERNAL_DIR}/glm)
target_include_directories (imported::fmt 
	INTERFACE
		${GLM_INCLUDE_PATH}
)
if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
	target_link_libraries (imported::fmt 
		INTERFACE
			debug ${ENLISTMENT_ROOT}/out/build/__externals/glm/glm/Debug/glm_static.lib
	)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	target_link_libraries (imported::fmt 
		INTERFACE
			debug ${ENLISTMENT_ROOT}/out/build/__externals/glm/glm/libglm_static.a
	)
endif ()