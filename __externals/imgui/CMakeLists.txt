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
find_package(Vulkan REQUIRED)

target_include_directories (imgui
    PUBLIC
        ./src
        ./src/backends
    PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        ${EXTERNAL_DIR}/glad/include
        ${EXTERNAL_DIR}/glad/include/KHR
)

target_include_directories(imgui PUBLIC ${EXTERNAL_DIR}/glfw/include)

