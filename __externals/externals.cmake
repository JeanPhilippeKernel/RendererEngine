# Packages
#
find_package(OpenGL REQUIRED)

# Libraries
#

# fmt
add_library (imported::fmt INTERFACE IMPORTED)
set (FMT_INCLUDE_PATH ${EXTERNAL_DIR}/fmt/include)
set_target_properties(imported::fmt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${FMT_INCLUDE_PATH}")
target_link_libraries(imported::fmt INTERFACE fmt)

# if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
# 	set_target_properties(imported::fmt PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/fmt/Debug/fmtd.lib"
# 	)
# elseif ((${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") OR (${CMAKE_SYSTEM_NAME} STREQUAL "Linux"))
# 	set_target_properties(imported::fmt PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/fmt/libfmtd.a"
# 	)
# endif ()

# glad
add_library (imported::glad INTERFACE IMPORTED)
set (GLAD_INCLUDE_PATH ${EXTERNAL_DIR}/glad/include ${EXTERNAL_DIR}/glad/include/KHR)
set_target_properties(imported::glad PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLAD_INCLUDE_PATH}")
target_link_libraries(imported::glad INTERFACE glad)

# if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
# 	set_target_properties(imported::glad PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/glad/Debug/glad.lib"
# 	)
# elseif ((${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") OR (${CMAKE_SYSTEM_NAME} STREQUAL "Linux"))
# 	set_target_properties(imported::glad PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/glad/libglad.a"
# 	)
# endif ()

# glm
add_library (imported::glm INTERFACE IMPORTED)
set (GLM_INCLUDE_PATH ${EXTERNAL_DIR}/glm)
set_target_properties(imported::glm PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_PATH}")
target_link_libraries(imported::glm INTERFACE glm)


# if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
# 	set_target_properties(imported::glm PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/glm/glm/Debug/glm_static.lib"
# 	)
# elseif ((${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") OR (${CMAKE_SYSTEM_NAME} STREQUAL "Linux"))
# 	set_target_properties(imported::glm PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/glm/glm/libglm_static.a"
# 	)
# endif ()

# imgui
add_library (imported::imgui INTERFACE IMPORTED)
set (IMGUI_INCLUDE_PATH ${EXTERNAL_DIR}/imgui/src)
set_target_properties(imported::imgui PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${IMGUI_INCLUDE_PATH}")
target_link_libraries(imported::imgui INTERFACE imgui)

# if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
# 	set_target_properties(imported::imgui PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/imgui/Debug/imgui.lib"
# 	)
# elseif ((${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") OR (${CMAKE_SYSTEM_NAME} STREQUAL "Linux"))
# 	set_target_properties(imported::imgui PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/imgui/libimgui.a"
# 	)
# endif ()

# SDL2
add_library (imported::sdl2 INTERFACE IMPORTED)
set (SDL2_INCLUDE_PATH ${EXTERNAL_DIR}/SDL2/include)
set_target_properties(imported::sdl2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_PATH}")
target_link_libraries(imported::sdl2 INTERFACE SDL2)

# if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
# 	set_target_properties(imported::sdl2 PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/SDL2/Debug/SDL2d.lib"
# 	)
# elseif ((${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") OR (${CMAKE_SYSTEM_NAME} STREQUAL "Linux"))
# 	set_target_properties(imported::sdl2 PROPERTIES
# 		IMPORTED_LOCATION_DEBUG "${OUTPUT_BUILD_DIR}/__externals/SDL2/libSDL2d.a"
# 	)
# endif ()

list (APPEND EXTERNAL_INCLUDE_DIRS 
	${FMT_INCLUDE_PATH} ${GLAD_INCLUDE_PATH} ${GLM_INCLUDE_PATH} ${IMGUI_INCLUDE_PATH} ${SDL2_INCLUDE_PATH}
)