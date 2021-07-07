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

# glad
add_library (imported::glad INTERFACE IMPORTED)
set (GLAD_INCLUDE_PATH ${EXTERNAL_DIR}/glad/include ${EXTERNAL_DIR}/glad/include/KHR)
set_target_properties(imported::glad PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLAD_INCLUDE_PATH}")
target_link_libraries(imported::glad INTERFACE glad)

# glm
add_library (imported::glm INTERFACE IMPORTED)
set (GLM_INCLUDE_PATH ${EXTERNAL_DIR}/glm)
set_target_properties(imported::glm PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_PATH}")
target_link_libraries(imported::glm INTERFACE glm)

# imgui
add_library (imported::imgui INTERFACE IMPORTED)
set (IMGUI_INCLUDE_PATH ${EXTERNAL_DIR}/imgui/src)
set_target_properties(imported::imgui PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${IMGUI_INCLUDE_PATH}")
target_link_libraries(imported::imgui INTERFACE imgui)

# SDL2
add_library (imported::sdl2 INTERFACE IMPORTED)
set (SDL2_INCLUDE_PATH ${EXTERNAL_DIR}/SDL2/include)
set_target_properties(imported::sdl2 PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_PATH}")
target_link_libraries(imported::sdl2 INTERFACE SDL2)

# Exporting all externals include directories 
list (APPEND EXTERNAL_INCLUDE_DIRS 
	${FMT_INCLUDE_PATH} ${GLAD_INCLUDE_PATH} ${GLM_INCLUDE_PATH} ${IMGUI_INCLUDE_PATH} ${SDL2_INCLUDE_PATH}
)