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

# Per-subsystem source globs — add platform-specific .cpp files by appending to
# the relevant variable inside the per-platform blocks further below, e.g.:
#   list(APPEND ZENGINE_SOURCES_WINDOWS
#       ${CMAKE_CURRENT_SOURCE_DIR}/Windows/Inputs/Platform/Win32/Win32Input.cpp)
#
file(GLOB_RECURSE ZENGINE_SOURCES_CORE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_PROFILING CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Profiling/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_RENDERING CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Rendering/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_WINDOWS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Windows/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_APPLICATION CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Applications/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Layers/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Controllers/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_MANAGERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Managers/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Serializers/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Importers/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_HELPERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Helpers/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Logging/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_EVENT CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Event/*.cpp
)
file(GLOB_RECURSE ZENGINE_SOURCES_HARDWARE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Hardwares/*.cpp
)

if(APPLE)
    list(APPEND ZENGINE_SOURCES_CRASH_HANDLERS
        ${CMAKE_CURRENT_SOURCE_DIR}/CrashHandlers/CrashHandlerMacOS.mm
    )
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    list(APPEND ZENGINE_SOURCES_CRASH_HANDLERS
        ${CMAKE_CURRENT_SOURCE_DIR}/CrashHandlers/CrashHandlerWindows.cpp
    )
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    list(APPEND ZENGINE_SOURCES_CRASH_HANDLERS
        ${CMAKE_CURRENT_SOURCE_DIR}/CrashHandlers/CrashHandlerLinux.cpp
    )
else()
    message(WARNING "CrashHandler: unsupported platform '${CMAKE_SYSTEM_NAME}' — crash reporting disabled")
endif()

file(GLOB_RECURSE ZENGINE_RESOURCES  CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/../Resources/Shaders/*.*)

source_group(TREE ${PROJECT_SOURCE_DIR}/ZEngine PREFIX "Source Files" FILES
    ${ZENGINE_HEADERS}
    ${ZENGINE_SOURCES_CORE}
    ${ZENGINE_SOURCES_PROFILING}
    ${ZENGINE_SOURCES_RENDERING}
    ${ZENGINE_SOURCES_WINDOWS}
    ${ZENGINE_SOURCES_APPLICATION}
    ${ZENGINE_SOURCES_MANAGERS}
    ${ZENGINE_SOURCES_HELPERS}
    ${ZENGINE_SOURCES_EVENT}
    ${ZENGINE_SOURCES_HARDWARE}
    ${ZENGINE_SOURCES_CRASH_HANDLERS}
)
source_group(TREE ${PROJECT_SOURCE_DIR}/../Resources PREFIX "Resources Files" FILES ${ZENGINE_RESOURCES})

# ZEngine source files
#
add_library(zEngineLib STATIC)

target_sources(zEngineLib PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_SOURCE_DIR})

target_sources(zEngineLib PRIVATE
    ${ZENGINE_SOURCES_CORE}
    ${ZENGINE_SOURCES_PROFILING}
    ${ZENGINE_SOURCES_RENDERING}
    ${ZENGINE_SOURCES_WINDOWS}
    ${ZENGINE_SOURCES_APPLICATION}
    ${ZENGINE_SOURCES_MANAGERS}
    ${ZENGINE_SOURCES_HELPERS}
    ${ZENGINE_SOURCES_EVENT}
    ${ZENGINE_SOURCES_HARDWARE}
    ${ZENGINE_SOURCES_CRASH_HANDLERS}
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine.cpp
)

if(ZENGINE_TRACY)
    target_compile_definitions(zEngineLib PRIVATE ZENGINE_TRACY=1)
endif()

target_compile_definitions (zEngineLib
    PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:ZENGINE_CRASH_HANDLER_ENABLED=1>
        $<$<CONFIG:Release>:ZENGINE_RELEASE=1>
        $<$<CONFIG:RelWithDebInfo>:ZENGINE_RELWITHDEBINFO=1>
        $<$<CONFIG:Debug>:ENABLE_VULKAN_VALIDATION_LAYER>
        $<$<CONFIG:Debug>:VMA_DEBUG_MARGIN=16>
        $<$<CONFIG:Debug>:VMA_DEBUG_DETECT_CORRUPTION=1>
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:ZENGINE_PROFILING=1>
	PUBLIC
		ZENGINE_PLATFORM
		ENABLE_VULKAN_SYNCHRONIZATION_LAYER
		YAML_CPP_STATIC_DEFINE
)

target_link_libraries (zEngineLib PUBLIC imported::ZEngine_External_Dependencies)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	target_link_libraries (zEngineLib PUBLIC imported::cppwinrt_headers WindowsApp.lib DbgHelp.lib User32.lib)
    target_compile_definitions(zEngineLib PUBLIC NOMINMAX GLFW_EXPOSE_NATIVE_WIN32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(zEngineLib PUBLIC stdc++fs dl)
    target_compile_definitions(zEngineLib PUBLIC GLFW_EXPOSE_NATIVE_WAYLAND)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(zEngineLib PRIVATE "-lobjc" "-framework AppKit" "-framework CoreGraphics")
    target_compile_definitions(zEngineLib PUBLIC GLFW_EXPOSE_NATIVE_COCOA)
endif()
