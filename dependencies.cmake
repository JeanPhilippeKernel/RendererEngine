include(FetchContent)

# Portable streaming SHA-256 for content-addressed cook objects. Exact upstream
# revision; no platform crypto/runtime dependency and no heap allocation. The
# inspected upstream source is dual Unlicense/0BSD; use the 0BSD option.
FetchContent_Declare(
  ze_sha256
  GIT_REPOSITORY https://github.com/amosnier/sha-2.git
  GIT_TAG 565f65009bdd98267361b17d50cddd7c9beb3e6c
)
FetchContent_MakeAvailable(ze_sha256)
add_library(ze_sha256 STATIC ${ze_sha256_SOURCE_DIR}/sha-256.c)
set_target_properties(ze_sha256 PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_include_directories(ze_sha256 PUBLIC ${ze_sha256_SOURCE_DIR})
install(FILES ${ze_sha256_SOURCE_DIR}/LICENSE.md DESTINATION licenses/sha-2)

# GLFW 3.5 can include both native Linux backends and selects the active one
# at runtime.  Clear the pre-3.4 cache option so an existing build directory
# can upgrade without GLFW rejecting its old GLFW_USE_WAYLAND entry.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  unset(GLFW_USE_WAYLAND CACHE)
  set(GLFW_BUILD_X11 ON CACHE BOOL "Build GLFW X11 backend" FORCE)
  set(GLFW_BUILD_WAYLAND ON CACHE BOOL "Build GLFW Wayland backend" FORCE)
endif()

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_SHALLOW TRUE
  GIT_TAG main
    )


# ImageCodec uses stb_image's internal per-format decoder API. Pin the exact
# revision tested by the bounded probe/decode path; upstream changes can alter
# those private signatures or remove supported formats without notice.
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/stb
  )

FetchContent_Declare(
  tinyexr
  GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
  GIT_TAG v1.0.13
  GIT_SHALLOW TRUE
  )

FetchContent_Declare(
  glfw3
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_SHALLOW TRUE
  GIT_TAG 3.5.1
  )

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_SHALLOW TRUE
  GIT_TAG v1.15.3 
  )

FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_SHALLOW TRUE
  GIT_TAG v6.0.5
  )

FetchContent_Declare(
  stduuid
  GIT_REPOSITORY https://github.com/mariusbancila/stduuid.git
  GIT_SHALLOW TRUE
   
  )

# Scene YAML parsing/emission uses status-returning C APIs, not C++ exceptions.
FetchContent_Declare(
  libyaml
  GIT_REPOSITORY https://github.com/yaml/libyaml.git
  GIT_SHALLOW TRUE
  GIT_TAG 0.2.5
  )
function(zengine_make_libyaml_available)
  # The upstream release uses generic options. Scope them to this dependency so
  # its command-line test programs and shared-library setting do not affect us.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  set(BUILD_TESTING OFF)
  set(BUILD_SHARED_LIBS OFF)
  FetchContent_MakeAvailable(libyaml)
  # Upstream has no allocator hooks. Wrap just api.c, the single allocation
  # boundary in pinned 0.2.5, without editing fetched sources or renaming libc
  # declarations. The bridge lives in yaml itself so standalone consumers work.
  get_target_property(yaml_sources yaml SOURCES)
  if(NOT "src/api.c" IN_LIST yaml_sources)
    message(FATAL_ERROR "libyaml source layout changed; review the checked allocator integration")
  endif()
  list(REMOVE_ITEM yaml_sources src/api.c)
  list(TRANSFORM yaml_sources PREPEND "${libyaml_SOURCE_DIR}/")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Scripts/CMake/YAMLAPI.c.in"
    "${libyaml_BINARY_DIR}/EngineYAMLAPI.c" @ONLY)
  set_property(TARGET yaml PROPERTY SOURCES ${yaml_sources})
  target_sources(yaml PRIVATE "${libyaml_BINARY_DIR}/EngineYAMLAPI.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ZEngine/ZEngine/Core/Serialization/YAMLAllocation.cpp")
  target_include_directories(yaml PRIVATE "${libyaml_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/ZEngine")
  target_compile_features(yaml PRIVATE cxx_std_20)
endfunction()
zengine_make_libyaml_available()

FetchContent_Declare(
  spirv_cross_core
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
  GIT_TAG vulkan-sdk-1.3.296.0
  GIT_SHALLOW TRUE
  )

FetchContent_Declare(
  VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
  GIT_SHALLOW TRUE
  GIT_TAG v3.3.0
)

FetchContent_Declare(
    SPIRV-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_SHALLOW TRUE
    GIT_TAG vulkan-sdk-1.3.296.0
)

FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_SHALLOW TRUE
    GIT_TAG 14.3.0
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/glslang"
    
)

FetchContent_Declare(
    SPIRV-Tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_SHALLOW TRUE
    GIT_TAG vulkan-sdk-1.3.296.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
Fetchcontent_Declare(
    GTest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_SHALLOW TRUE
    GIT_TAG v1.17.0
)

Fetchcontent_Declare(
  nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_SHALLOW TRUE
     
)

Fetchcontent_Declare(
    tlsf
    GIT_REPOSITORY https://github.com/mattconte/tlsf
    GIT_SHALLOW TRUE
    SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/tlsf
)
FetchContent_GetProperties(tlsf)
if(NOT tlsf_POPULATED)
    FetchContent_Populate(tlsf)
endif()
add_library(tlsf STATIC ${FETCHCONTENT_BASE_DIR}/tlsf/tlsf.c)
target_include_directories(tlsf PUBLIC ${FETCHCONTENT_BASE_DIR}/tlsf)

Fetchcontent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11
    GIT_SHALLOW TRUE
    GIT_TAG main
    SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/CLI11
    
)


Fetchcontent_Declare(
    rapidhash
    GIT_REPOSITORY https://github.com/Nicoshev/rapidhash
    GIT_SHALLOW TRUE
    SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/rapidhash
    
)

FetchContent_Declare(Vulkan-Headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers
    GIT_SHALLOW TRUE
    GIT_TAG vulkan-sdk-1.3.296.0
)
	
FetchContent_Declare(Vulkan-Loader
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader
    GIT_SHALLOW TRUE
    GIT_TAG vulkan-sdk-1.3.296.0
)

FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz
    GIT_SHALLOW TRUE
)

FetchContent_Declare(ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_SHALLOW    TRUE
    GIT_TAG        v0.14.3
    SOURCE_DIR     ${FETCHCONTENT_BASE_DIR}/ufbx
)
FetchContent_GetProperties(ufbx)
if(NOT ufbx_POPULATED)
    FetchContent_Populate(ufbx)
endif()
add_library(ufbx STATIC ${FETCHCONTENT_BASE_DIR}/ufbx/ufbx.c)
target_include_directories(ufbx PUBLIC ${FETCHCONTENT_BASE_DIR}/ufbx)

FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_SHALLOW    TRUE
    GIT_TAG        v0.22
    SOURCE_DIR     ${FETCHCONTENT_BASE_DIR}/meshoptimizer
)
FetchContent_GetProperties(meshoptimizer)
if(NOT meshoptimizer_POPULATED)
    FetchContent_Populate(meshoptimizer)
endif()
file(GLOB MESHOPT_SOURCES ${FETCHCONTENT_BASE_DIR}/meshoptimizer/src/*.cpp)
add_library(meshoptimizer STATIC ${MESHOPT_SOURCES})
target_include_directories(meshoptimizer PUBLIC ${FETCHCONTENT_BASE_DIR}/meshoptimizer/src)

FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_SHALLOW    TRUE
    GIT_TAG        v3.12.3
    SOURCE_DIR     ${FETCHCONTENT_BASE_DIR}/simdjson
)
set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_SHALLOW    TRUE
    GIT_TAG        VER-2-13-3
)
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)

FetchContent_Declare(fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_SHALLOW    TRUE
    GIT_TAG        v0.9.0
)

FetchContent_Declare(TracyClient
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_SHALLOW TRUE
    GIT_TAG v0.13.1
)

if(ZENGINE_TRACY)
    set(TRACY_ENABLE    ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(
  freetype
  fmt
  Vulkan-Headers
  Vulkan-Loader
  stb
  glfw3
  spdlog
  assimp
  stduuid
  VulkanMemoryAllocator
  SPIRV-Headers
  spirv_cross_core
  nlohmann_json
  CLI11
  rapidhash
  SPIRV-Tools
  glslang
  GTest
  miniz
  TracyClient
  simdjson
  fastgltf
  )

if(APPLE)
  # These dependency targets link only system libraries in this configuration.
  # Do not inherit the root's absolute install rpath: the frameworks do not need
  # it, and adding it at install time can exceed their Mach-O header capacity.
  # Consumers locate the @rpath install IDs through their own loader-relative
  # paths. Keep fetched sources untouched and configure their real targets.
  foreach(_system_dependency IN ITEMS freetype vulkan vulkan-framework)
    if(TARGET ${_system_dependency})
      set_target_properties(${_system_dependency} PROPERTIES INSTALL_RPATH "")
    endif()
  endforeach()

  # Upstream install rules precede these hooks and can edit load commands.
  foreach(_installed_library IN ITEMS vulkan SPIRV-Tools-shared)
    if(TARGET ${_installed_library})
      zengine_sign_installed_target(${_installed_library} "${CMAKE_INSTALL_LIBDIR}")
    endif()
  endforeach()
  foreach(_installed_tool IN ITEMS spirv-cross glslang-standalone spirv-remap)
    if(TARGET ${_installed_tool})
      zengine_sign_installed_target(${_installed_tool} "${CMAKE_INSTALL_BINDIR}")
    endif()
  endforeach()
endif()

# TinyEXR's bundled CMake project declares another target named miniz. Fetch
# only its source instead, then build it against the Miniz target already used
# by the engine.
FetchContent_GetProperties(tinyexr)
if(NOT tinyexr_POPULATED)
    FetchContent_Populate(tinyexr)
endif()
add_library(tinyexr STATIC ${tinyexr_SOURCE_DIR}/tinyexr.cc)
target_include_directories(tinyexr PUBLIC ${tinyexr_SOURCE_DIR})
target_link_libraries(tinyexr PRIVATE miniz ${CMAKE_DL_LIBS})

foreach(_spirv_target IN ITEMS
    SPIRV-Tools SPIRV-Tools-static SPIRV-Tools-shared
    SPIRV-Tools-opt SPIRV-Tools-reduce SPIRV-Tools-link
    SPIRV-Tools-lint SPIRV-Tools-diff SPIRV-Tools-fuzz
    spirv-val spirv-opt spirv-dis spirv-as spirv-cfg)

    if(TARGET ${_spirv_target})
        get_target_property(_real ${_spirv_target} ALIASED_TARGET)
        if(NOT _real)
            set(_real ${_spirv_target})
        endif()
        set_target_properties(${_real} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    endif()
endforeach()


add_library(External_libs INTERFACE)

target_include_directories(External_libs
    INTERFACE
                                ${CMAKE_CURRENT_SOURCE_DIR}
                                ${FETCHCONTENT_BASE_DIR}
                                ${FETCHCONTENT_BASE_DIR}/rapidhash
                                ${FETCHCONTENT_BASE_DIR}/stb
                                ${FETCHCONTENT_BASE_DIR}/CLI11
                                ${FETCHCONTENT_BASE_DIR}/tlsf
                                ${FETCHCONTENT_BASE_DIR}/ufbx
                                ${FETCHCONTENT_BASE_DIR}/meshoptimizer/src
                       )


if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_link_libraries(External_libs INTERFACE SPIRV-Tools-static)
else()
    target_link_libraries(External_libs INTERFACE SPIRV-Tools)
endif()


target_link_libraries(External_libs
    INTERFACE
         Vulkan::Headers
         Vulkan::Loader
         glfw
         fmt::fmt
         spdlog::spdlog
         assimp::assimp
         stduuid
         yaml
         spirv-cross-core
         SPIRV-Tools-opt
         glslang::glslang
         glslang::glslang-default-resource-limits
         glslang::SPIRV
         GPUOpen::VulkanMemoryAllocator
         nlohmann_json::nlohmann_json
         miniz
         fastgltf::fastgltf
         tinyexr
         ufbx
         meshoptimizer
         freetype
         tlsf
         ze_sha256
)

if(ZENGINE_TRACY)
    target_link_libraries(External_libs INTERFACE TracyClient)
endif()

add_library(imported::ZEngine_External_Dependencies ALIAS External_libs)

add_library(External_obeliskLibs INTERFACE)
target_link_libraries(External_obeliskLibs 
    INTERFACE 
    CLI11::CLI11
)

add_library(imported::External_obeliskLibs ALIAS External_obeliskLibs)

include(${CMAKE_CURRENT_SOURCE_DIR}/Scripts/CMake/NuGet.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/Scripts/CMake/CppWinRT.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/Scripts/CMake/LoggingDefaults.cmake)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    # Install necessary NuGet dependencies
    install_nuget_package(Microsoft.Windows.CppWinRT 2.0.240405.15 CPPWINRT_NUGET_PATH)

    # Generate CppWinRT headers for the local OS
    generate_winrt_headers(
        EXECUTABLE
            ${CPPWINRT_NUGET_PATH}/bin/cppwinrt.exe
        INPUT
            local
        OUTPUT
            ${CMAKE_BINARY_DIR}/__winrt
        OPTIMIZE
    )

    add_library(imported::cppwinrt_headers INTERFACE IMPORTED)
    target_include_directories(imported::cppwinrt_headers INTERFACE
        ${CMAKE_BINARY_DIR}/__winrt
    )
endif()
