
add_library (imgui
    STATIC
        src/imgui.h
        src/imgui_internal.h
		src/imgui.cpp
		src/imgui_tables.cpp
		src/imgui_widgets.cpp
		src/imgui_demo.cpp
		src/imgui_draw.cpp
		src/backends/imgui_impl_opengl3.cpp
		src/backends/imgui_impl_sdl.cpp
)

target_include_directories (imgui
    PUBLIC
        ./src
        ./src/backends
        ${EXTERNAL_DIR}/SDL2/include
    PRIVATE
        ${EXTERNAL_DIR}/glad/include
        ${EXTERNAL_DIR}/glad/include/KHR
)

target_compile_definitions (imgui
	PUBLIC
		IMGUI_IMPL_OPENGL_LOADER_GLAD
)

