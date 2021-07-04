cmake_minimum_required (VERSION 3.17)

project (ZEngine 
	VERSION 1.0
	DESCRIPTION "Z-Engine is the core engine lib that provides abstraction on top of OpenGL/DirectX/Metal"
	LANGUAGES CXX C
)

set (CMAKE_CXX_STANDARD_REQUIRED ON) 

if (MSVC)
	set(CMAKE_CXX_STANDARD 17)
else ()
	if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
		add_compile_options(-fpermissive)
	endif ()
	set(CMAKE_CXX_STANDARD 20)
endif ()

set(CMAKE_C_STANDARD 11)

add_subdirectory (src)