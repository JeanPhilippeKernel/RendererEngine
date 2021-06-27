add_subdirectory(Layers)

add_executable (sandbox3d Sandbox.cpp)

target_link_libraries (sandbox3d PRIVATE exampleLayerLib3d)