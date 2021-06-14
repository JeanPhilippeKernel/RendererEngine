cmake_minimum_required (VERSION 3.17)

project (ZEngine 
	VERSION 1.0
	DESCRIPTION "Z-Engine is the core engine lib that provides abstraction on top of OpenGL/DirectX/Metal"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory (src)