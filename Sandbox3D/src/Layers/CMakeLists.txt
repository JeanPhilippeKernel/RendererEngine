file (GLOB HEADER_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library (exampleLayerLib3d 
	STATIC 
		 ExampleLayer.cpp
		 ${HEADER_FILES}
)

target_include_directories (exampleLayerLib3d
	PUBLIC 
		.
		${ENLISTMENT_ROOT}/ZEngine/include
)

target_link_directories (exampleLayerLib3d
	PRIVATE
		${ENLISTMENT_ROOT}/out/build/ZEngine/src/Debug
)

target_link_libraries (exampleLayerLib3d 
	PUBLIC 
		zEngineLib
)