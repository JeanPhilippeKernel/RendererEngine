# Packages
#
set(EXTERNAL_INCLUDE_DIRS
	${IN_ENGINE_VULKAN_HEADER_PATH}
	${EXTERNAL_DIR}/fmt/include
	${EXTERNAL_DIR}/imgui/src
	${EXTERNAL_DIR}/spdlog/include
	${EXTERNAL_DIR}/glfw/include
	${EXTERNAL_DIR}/entt/single_include
	${EXTERNAL_DIR}/assimp/include
	${EXTERNAL_DIR}/stduuid/include
	${EXTERNAL_DIR}/yaml-cpp/include
	${EXTERNAL_DIR}/SPIRV-headers
	${EXTERNAL_DIR}/SPIRV-Tools
	${EXTERNAL_DIR}/glslang
	${EXTERNAL_DIR}/SPIRV-Cross
	${EXTERNAL_DIR}/VulkanMemoryAllocator
	${EXTERNAL_DIR}/nlohmann_json/single_include
)

if (MSVC)
	target_compile_options(assimp PRIVATE /Wv:18) # Fix zip lib compile issue
endif ()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(imgui PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library (imported::External_libs INTERFACE IMPORTED)

target_link_libraries(imported::External_libs INTERFACE
	vulkan
	fmt
	imgui
	spdlog
	glfw
	EnTT
	assimp
	stduuid
	yaml-cpp
	spirv-cross-reflect spirv-cross-glsl
	GPUOpen::VulkanMemoryAllocator
	glslang  SPIRV
	SPIRV-Tools
)
