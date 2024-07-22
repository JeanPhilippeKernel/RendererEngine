set(CMAKE_CXX_STANDARD 11)

list (APPEND IMGUI_SOURCE
        src/imgui.h
        src/imgui_internal.h
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
add_library (imgui STATIC ${IMGUI_SOURCE})

# include directories
#
target_include_directories (imgui
    PUBLIC
        ./src
        ./src/backends
        ${EXTERNAL_DIR}/glfw/include
    PRIVATE
        ${EXTERNAL_DIR}/Vulkan-Headers/build/install/include
)

