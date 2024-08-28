cmake_minimum_required (VERSION 3.17)

project (Tetragrama 
	VERSION 1.0
	DESCRIPTION "Tetragrama, the ZEngine Editor"
	LANGUAGES CXX
)

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

add_subdirectory (src)