cmake_minimum_required (VERSION 3.17)

project (ZEngine 
	VERSION 1.0
	DESCRIPTION "ZEngine is the core engine lib that provides abstraction on top of OpenGL/DirectX/Metal"
	LANGUAGES CXX C
)

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

add_subdirectory (ZEngine)
add_subdirectory (tests)
