# Parse version components and generate version.h
#
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)([-+](.*))?$" _ver_match "${ZENGINE_VERSION}")
set(ZENGINE_VERSION_MAJOR    ${CMAKE_MATCH_1})
set(ZENGINE_VERSION_MINOR    ${CMAKE_MATCH_2})
set(ZENGINE_VERSION_PATCH    ${CMAKE_MATCH_3})
set(ZENGINE_VERSION_PRERELEASE ${CMAKE_MATCH_5})

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE ZENGINE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT ZENGINE_GIT_HASH)
    set(ZENGINE_GIT_HASH "unknown")
endif()

if(ZENGINE_VERSION_PRERELEASE)
    set(ZENGINE_VERSION_FULL "${ZENGINE_VERSION_MAJOR}.${ZENGINE_VERSION_MINOR}.${ZENGINE_VERSION_PATCH}-${ZENGINE_VERSION_PRERELEASE}")
else()
    set(ZENGINE_VERSION_FULL "${ZENGINE_VERSION_MAJOR}.${ZENGINE_VERSION_MINOR}.${ZENGINE_VERSION_PATCH}")
endif()

message("-- Engine version: v${ZENGINE_VERSION_FULL}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/Core/version.h
    @ONLY
)

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
        ${CMAKE_CURRENT_BINARY_DIR}
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
