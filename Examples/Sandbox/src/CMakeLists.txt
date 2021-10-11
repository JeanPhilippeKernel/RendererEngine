include (${ENLISTMENT_ROOT}/dependencies.cmake)

add_executable (sandbox2d 
    Layers/ExampleLayer.h
    Layers/ExampleLayer.cpp
    Components/AboutUIComponent.h
    Components/DemoUIComponent.h
    Sandbox.cpp
)

# We set this debugger directory to find assets and resources file
# after being copied to Debug and Release output directories 
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(sandbox2d PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)$(Configuration)")
endif ()

target_include_directories (sandbox2d
	PRIVATE 
		Layers
        Components
)

target_compile_definitions (sandbox2d
	PRIVATE
		IMGUI_IMPL_OPENGL_LOADER_GLAD
) 

target_link_libraries(sandbox2d
    PUBLIC
        imported::zEngineLib
)