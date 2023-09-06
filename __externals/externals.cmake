# Packages
#

# vulkan
find_package(Vulkan REQUIRED)

add_library(imported::vulkan INTERFACE IMPORTED)
target_include_directories(imported::vulkan INTERFACE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(imported::vulkan INTERFACE Vulkan::Vulkan)

# fmt
add_library (imported::fmt INTERFACE IMPORTED)
set (FMT_INCLUDE_PATH ${EXTERNAL_DIR}/fmt/include)
set_target_properties(imported::fmt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${FMT_INCLUDE_PATH}")
target_link_libraries(imported::fmt INTERFACE fmt)

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
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(imported::imgui INTERFACE ${CMAKE_DL_LIBS})
endif()

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

if (MSVC)
	target_compile_options(assimp PRIVATE /Wv:18) # Fix zip lib compile issue
endif ()

target_link_libraries(imported::assimp INTERFACE assimp)

# stduuid
add_library (imported::stduuid INTERFACE IMPORTED)
set (STDUUID_INCLUDE_PATH ${EXTERNAL_DIR}/stduuid/include)
set_target_properties(imported::stduuid PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${STDUUID_INCLUDE_PATH}")
target_link_libraries(imported::stduuid INTERFACE stduuid)


# yaml-cpp
add_library (imported::yaml-cpp INTERFACE IMPORTED)
set (YAML_CPP_INCLUDE_PATH ${EXTERNAL_DIR}/yaml-cpp/include)
set_target_properties(imported::yaml-cpp PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_INCLUDE_PATH}")
target_link_libraries(imported::yaml-cpp INTERFACE yaml-cpp)

# Exporting all externals include directories
list (APPEND EXTERNAL_INCLUDE_DIRS
	${FMT_INCLUDE_PATH}
	${GLM_INCLUDE_PATH}
	${IMGUI_INCLUDE_PATH}
	${SPDLOG_INCLUDE_PATH}
	${GLFW_INCLUDE_PATH}
	${ENTT_INCLUDE_PATH}
	${ASSIMP_INCLUDE_PATH}
	${STDUUID_INCLUDE_PATH}
	${YAML_CPP_INCLUDE_PATH}
)
