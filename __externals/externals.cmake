# Packages
#
find_package(OpenGL REQUIRED)

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
target_link_libraries(imported::sdl2 INTERFACE SDL2-static)

# spdlog
add_library (imported::spdlog INTERFACE IMPORTED)
set (SPDLOG_INCLUDE_PATH ${EXTERNAL_DIR}/spdlog/include)
set_target_properties(imported::spdlog PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${SPDLOG_INCLUDE_PATH}")
target_link_libraries(imported::spdlog INTERFACE spdlog)

# glfw
add_library (imported::glfw INTERFACE IMPORTED)
set (GLFW_INCLUDE_PATH ${EXTERNAL_DIR}/glfw/include)
set_target_properties(imported::glfw PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLFW_INCLUDE_PATH}")
target_link_libraries(imported::glfw INTERFACE glfw)

# entt
add_library (imported::entt INTERFACE IMPORTED)
set (ENTT_INCLUDE_PATH ${EXTERNAL_DIR}/entt/single_include)
set_target_properties(imported::entt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${ENTT_INCLUDE_PATH}")
target_link_libraries(imported::entt INTERFACE EnTT)

# assimp
add_library (imported::assimp INTERFACE IMPORTED)
set (ASSIMP_INCLUDE_PATH ${EXTERNAL_DIR}/assimp/include)
set_target_properties(imported::assimp PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_INCLUDE_PATH}")
target_compile_options(assimp PRIVATE /Wv:18) # Fix zip lib compile issue
target_link_libraries(imported::assimp INTERFACE assimp)

# Exporting all externals include directories
list (APPEND EXTERNAL_INCLUDE_DIRS
	${FMT_INCLUDE_PATH}
	${GLAD_INCLUDE_PATH}
	${GLM_INCLUDE_PATH}
	${IMGUI_INCLUDE_PATH}
	${SDL2_INCLUDE_PATH}
	${SPDLOG_INCLUDE_PATH}
	${GLFW_INCLUDE_PATH}
	${ENTT_INCLUDE_PATH}
	${ASSIMP_INCLUDE_PATH}
)