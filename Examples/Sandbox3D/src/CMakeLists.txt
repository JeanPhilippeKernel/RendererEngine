include (${ENLISTMENT_ROOT}/dependencies.cmake)

add_executable (sandbox3d 
    Layers/ExampleLayer.h
    Layers/ExampleLayer.cpp
    Sandbox.cpp
)

target_include_directories (sandbox3d
	PRIVATE 
		./Layers
		${ENLISTMENT_ROOT}/ZEngine/include
)

target_compile_definitions (sandbox3d
	PRIVATE
		IMGUI_IMPL_OPENGL_LOADER_GLAD
)

target_link_libraries(sandbox3d
    PRIVATE
        imported::zEngineLib
)