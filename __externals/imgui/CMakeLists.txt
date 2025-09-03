set(CMAKE_CXX_STANDARD 11)

list (APPEND IMGUI_SOURCE
		src/imgui.cpp
		src/imgui_tables.cpp
		src/imgui_widgets.cpp
		src/imgui_demo.cpp
		src/imgui_draw.cpp
		src/backends/imgui_impl_vulkan.cpp
        src/backends/imgui_impl_glfw.cpp
)

# Source files
#
add_library (imgui STATIC )

target_sources(imgui PRIVATE ${IMGUI_SOURCE})

# include directories
#
target_include_directories (imgui
    PUBLIC
        ./src
        ./src/backends
)

target_link_libraries(imgui PUBLIC glfw Vulkan::Headers Vulkan::Loader)

