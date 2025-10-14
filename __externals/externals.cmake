# Packages
#

if (MSVC)
	target_compile_options(assimp PRIVATE /Wv:18) # Fix zip lib compile issue
else()
	target_compile_options(assimp PRIVATE -Wno-shorten-64-to-32 -Wno-unused-but-set-variable -Wno-deprecated-declarations)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	target_link_libraries(imgui PUBLIC ${CMAKE_DL_LIBS})
endif()

add_library (imported::External_libs INTERFACE IMPORTED)
add_library (imported::External_obeliskLibs INTERFACE IMPORTED)


target_link_libraries(imported::External_obeliskLibs INTERFACE
	CLI11::CLI11
)

target_link_libraries(imported::External_libs INTERFACE
         fmt::fmt
         imgui
         spdlog::spdlog
         EnTT::EnTT
         assimp::assimp
         stduuid
         yaml-cpp::yaml-cpp
         spirv-cross-core
         SPIRV-Tools
         glslang
         glslang-default-resource-limits
         SPIRV
         SPVRemapper
         GPUOpen::VulkanMemoryAllocator 
         nlohmann_json::nlohmann_json
         rapidhash
         Vulkan::Loader
)
