file (GLOB HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library (exampleLayerLib2d 
	STATIC 
		 ExampleLayer.cpp
		 ${HEADER_FILES}
)


target_include_directories (exampleLayerLib2d
	PUBLIC 
		.
		${ENLISTMENT_ROOT}/ZEngine/include
)
if (${CMAKE_SYSTEM_NAME} STREQUAL Windows)
	target_link_directories (exampleLayerLib2d
		PRIVATE
			${ENLISTMENT_ROOT}/out/build/ZEngine/src/Debug
	)
	target_link_libraries (exampleLayerLib2d 
		PUBLIC 
			zEngineLib
	)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	target_link_directories (exampleLayerLib2d
		PRIVATE
			${ENLISTMENT_ROOT}/out/build/ZEngine/src
	)
	target_link_libraries (exampleLayerLib2d 
		PUBLIC 
			libzEngineLib
	)
endif()