file (GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS include/*.h)

add_library (glad
	STATIC
		${HEADER_FILES}
		src/glad.c
)

target_include_directories (glad
	PUBLIC 
		./include/glad
		./include/KHR
)