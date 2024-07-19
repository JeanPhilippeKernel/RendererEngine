# Packages
#
set(EXTERNAL_INCLUDE_DIRS
	${EXTERNAL_DIR}/Vulkan-Headers/build/install/include
	${EXTERNAL_DIR}/fmt/include
	${EXTERNAL_DIR}/glm/glm
	${EXTERNAL_DIR}/imgui/src
	${EXTERNAL_DIR}/spdlog/include
	${EXTERNAL_DIR}/glfw/include
	${EXTERNAL_DIR}/entt
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
elseif(APPLE)
	target_compile_options(assimp PRIVATE -Wno-shorten-64-to-32 -Wno-unused-but-set-variable -Wno-deprecated-declarations)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(imgui PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library (imported::External_libs INTERFACE IMPORTED)

target_link_libraries(imported::External_libs INTERFACE
	vulkan
	fmt
	glm
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
