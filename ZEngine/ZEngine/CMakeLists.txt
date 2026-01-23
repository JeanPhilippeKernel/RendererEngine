file (GLOB_RECURSE HEADER_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
file (GLOB_RECURSE CPP_FILES_LIST CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file (GLOB_RECURSE RESOURCE_FILES_LIST CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/../Resources/Shaders/*.*)

source_group (TREE ${PROJECT_SOURCE_DIR}/ZEngine PREFIX "Source Files" FILES ${HEADER_FILES_LIST} ${CPP_FILES_LIST})
source_group (TREE ${PROJECT_SOURCE_DIR}/../Resources PREFIX "Resources Files" FILES ${RESOURCE_FILES_LIST})

# ZEngine source files
#
add_library (zEngineLib STATIC)

target_sources(zEngineLib PRIVATE ${CPP_FILES_LIST})

target_include_directories (zEngineLib
	PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
		./Applications
		./Core
		./Core/Maths
		./Core/Memory
		./Core/Containers
		./Hardwares
		./Helpers
		./Layers
		./Managers
		./Importers
		./Controllers
		./Logging
		./Rendering
		./Rendering/Entities
		./Rendering/Meshes
		./Rendering/Buffers
		./Rendering/Cameras
		./Rendering/Pools
		./Rendering/Primitives
		./Rendering/Renderers
		./Rendering/Renderers/Pipelines
		./Rendering/Renderers/RenderPasses
		./Rendering/Renderers/Storages
		./Rendering/Scenes
		./Rendering/Shaders
		./Rendering/Shaders/Compilers
		./Rendering/Specifications
		./Rendering/Textures
		./Windows
		./Windows/Events
		./Windows/Inputs
		./Windows/Layers
		./Managers
		./Serializers
)

target_precompile_headers(zEngineLib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	target_compile_definitions (zEngineLib PUBLIC ENABLE_VULKAN_VALIDATION_LAYER)
endif()

target_compile_definitions (zEngineLib
	PUBLIC
		ZENGINE_PLATFORM
		ENABLE_VULKAN_SYNCHRONIZATION_LAYER
		YAML_CPP_STATIC_DEFINE
)

target_link_libraries (zEngineLib PUBLIC imported::ZEngine_External_Dependencies)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	target_link_libraries (zEngineLib PUBLIC imported::cppwinrt_headers WindowsApp.lib)
    target_compile_definitions(zEngineLib PUBLIC NOMINMAX GLFW_EXPOSE_NATIVE_WIN32)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(zEngineLib PUBLIC stdc++fs)
    target_compile_definitions(zEngineLib PUBLIC GLFW_EXPOSE_NATIVE_WAYLAND)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(zEngineLib PUBLIC GLFW_EXPOSE_NATIVE_COCOA)
endif()
