cmake_minimum_required (VERSION 3.17)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION.txt" ZENGINE_VERSION)
string(STRIP "${ZENGINE_VERSION}" ZENGINE_VERSION)

project (ZEngine
	VERSION ${ZENGINE_VERSION}
	DESCRIPTION "ZEngine is the core engine lib that provides abstraction on top of OpenGL/DirectX/Metal"
	LANGUAGES CXX C
)

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

add_subdirectory (ZEngine)
add_subdirectory (tests)
