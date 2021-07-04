include (${ENLISTMENT_ROOT}/dependencies.cmake)

add_executable (sandbox2d 
    Layers/ExampleLayer.h
    Layers/ExampleLayer.cpp
    Sandbox.cpp
)

target_include_directories (sandbox2d
	PRIVATE 
		Layers
)

target_compile_definitions (sandbox2d
	PRIVATE
		IMGUI_IMPL_OPENGL_LOADER_GLAD
) 

target_link_libraries(sandbox2d
    PUBLIC
        imported::zEngineLib
)