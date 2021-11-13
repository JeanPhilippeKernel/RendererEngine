

list (APPEND IMGUI_SOURCE 
        src/imgui.h
        src/imgui_internal.h
		src/imgui.cpp
		src/imgui_tables.cpp
		src/imgui_widgets.cpp
		src/imgui_demo.cpp
		src/imgui_draw.cpp
		src/backends/imgui_impl_opengl3.cpp
)

# Source files
#
if (USE_SDL_LIBS)
    list (APPEND IMGUI_SOURCE  src/backends/imgui_impl_sdl.cpp)
else ()
    list (APPEND IMGUI_SOURCE  src/backends/imgui_impl_glfw.cpp)
endif ()

add_library (imgui STATIC ${IMGUI_SOURCE})

# include directories
#

target_include_directories (imgui
    PUBLIC
        ./src
        ./src/backends
    PRIVATE
        ${EXTERNAL_DIR}/glad/include
        ${EXTERNAL_DIR}/glad/include/KHR
)

if (USE_SDL_LIBS)
    target_include_directories(imgui PUBLIC ${EXTERNAL_DIR}/SDL2/include)
else ()
    target_include_directories(imgui PUBLIC ${EXTERNAL_DIR}/glfw/include)
endif ()

