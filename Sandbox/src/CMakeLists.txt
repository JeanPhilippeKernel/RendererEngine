add_subdirectory(Layers)

add_executable (sandbox2d Sandbox.cpp)

target_link_libraries (sandbox2d PRIVATE exampleLayerLib2d)