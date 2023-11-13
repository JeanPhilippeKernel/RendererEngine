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


list (APPEND Sources
		GlfwWindow/VulkanWindow.cpp
)

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
		# We should control it by using buildEngine parameter
		ENABLE_VULKAN_VALIDATION_LAYER
		ENABLE_VULKAN_SYNCHRONIZATION_LAYER
		YAML_CPP_STATIC_DEFINE
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	target_compile_definitions (zEngineLib PUBLIC VK_USE_PLATFORM_WIN32_KHR)
endif()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(zEngineLib PRIVATE stdc++fs)
endif ()

target_link_libraries (zEngineLib PUBLIC imported::glfw)

target_link_libraries (zEngineLib
	PUBLIC
		imported::fmt
		imported::spdlog
		imported::glm
		imported::imgui
		imported::entt
		imported::assimp
		imported::stduuid
		imported::yaml-cpp
		imported::vulkan
		imported::SPIRV-Cross
)