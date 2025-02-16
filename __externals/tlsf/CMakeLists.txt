set(CMAKE_CXX_STANDARD 11)

list (APPEND TLSF_SOURCE
        src/tlsf.h
		src/tlsf.c
)

# Source files
#
add_library (tlsf STATIC ${TLSF_SOURCE})

# include directories
#
target_include_directories (tlsf
    PUBLIC
        ./src
)

