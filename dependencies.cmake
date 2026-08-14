include(FetchContent)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_SHALLOW TRUE
  GIT_TAG main
    )

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_SHALLOW TRUE
  GIT_TAG v1.89.9-docking
  SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/imgui"
  )

FetchContent_Declare(
  imguizmo
  GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
  GIT_SHALLOW TRUE
  GIT_TAG 1.83
  SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/ImGuizmo"
  )

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_SHALLOW TRUE
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/stb
  )

FetchContent_Declare(
  glfw3
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_SHALLOW TRUE
  GIT_TAG 3.3.10
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

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp
  GIT_SHALLOW TRUE
  GIT_TAG yaml-cpp-0.9.0
  )


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

set(YAML_MSVC_SHARED_RT ON CACHE BOOL "" FORCE)
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
  fmt
  Vulkan-Headers
  Vulkan-Loader
  imgui
  ImGuizmo
  stb
  glfw3
  spdlog
  assimp
  stduuid
  yaml-cpp
  VulkanMemoryAllocator
  SPIRV-Headers
  spirv_cross_core
  nlohmann_json
  tlsf
  CLI11
  rapidhash
  SPIRV-Tools
  glslang
  GTest
  miniz
  TracyClient
  fastgltf
  )

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

set(IMGUIDIR ${FETCHCONTENT_BASE_DIR}/imgui)

add_library(imgui STATIC)

target_sources(
  imgui
  PRIVATE ${IMGUIDIR}/imgui.cpp
          ${IMGUIDIR}/imgui_demo.cpp
          ${IMGUIDIR}/imgui_draw.cpp
          ${IMGUIDIR}/imgui_tables.cpp
          ${IMGUIDIR}/imgui_widgets.cpp
          ${IMGUIDIR}/misc/cpp/imgui_stdlib.cpp
          ${IMGUIDIR}/backends/imgui_impl_glfw.cpp
          ${IMGUIDIR}/backends/imgui_impl_vulkan.cpp)

      target_include_directories(imgui 
          PUBLIC ${FETCHCONTENT_BASE_DIR}
          PUBLIC ${FETCHCONTENT_BASE_DIR}/imgui
      )

target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_VULKAN IMGUI_DEFINE_MATH_OPERATORS)

target_link_libraries(imgui PRIVATE glfw Vulkan::Headers Vulkan::Loader)

add_library(imguizmo STATIC)

target_sources(imguizmo
    PRIVATE ${FETCHCONTENT_BASE_DIR}/ImGuizmo/ImGuizmo.cpp)

target_include_directories(imguizmo
                           PUBLIC ${FETCHCONTENT_BASE_DIR}/imguizmo-src)

target_link_libraries(imguizmo PUBLIC imgui)

add_library(External_libs INTERFACE)

target_include_directories(External_libs
    INTERFACE
                                ${CMAKE_CURRENT_SOURCE_DIR}
                                ${FETCHCONTENT_BASE_DIR}
                                ${FETCHCONTENT_BASE_DIR}/rapidhash
                                ${FETCHCONTENT_BASE_DIR}/stb
                                ${FETCHCONTENT_BASE_DIR}/CLI11
                                ${FETCHCONTENT_BASE_DIR}/tlsf
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
         imguizmo
         spdlog::spdlog
         assimp::assimp
         stduuid
         yaml-cpp::yaml-cpp
         spirv-cross-core
         SPIRV-Tools-opt
         glslang::glslang
         glslang::glslang-default-resource-limits
         glslang::SPIRV
         GPUOpen::VulkanMemoryAllocator
         nlohmann_json::nlohmann_json
         miniz
         fastgltf::fastgltf
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
