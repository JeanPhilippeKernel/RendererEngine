# Selecting Compiler 
#
if (APPLE)
	set (CMAKE_CXX_COMPILER "/usr/bin/clang++")
	set (CMAKE_C_COMPILER 	"/usr/bin/clang")
    return()
elseif (UNIX)
    set (CMAKE_CXX_COMPILER "/usr/bin/clang++-7")
    set (CMAKE_C_COMPILER 	"/usr/bin/clang-7")
endif ()
