cmake_minimum_required (VERSION 3.23)

project (Obelisk 
	VERSION 1.0
	DESCRIPTION "Obelisk, The game application entry point"
	LANGUAGES CXX
)

set (CMAKE_CXX_STANDARD_REQUIRED ON)
set	(CMAKE_CXX_STANDARD 20)

set (TARGET_NAME Obelisk)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_executable (${TARGET_NAME} WIN32)
else()
    add_executable (${TARGET_NAME})
endif()

target_sources(${TARGET_NAME} PUBLIC EntryPoint.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS ${PROJECT_DIR})

# We set this debugger directory to find assets and resources file
# after being copied to Debug and Release output directories 
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set_target_properties(${TARGET_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$(ProjectDir)$(Configuration)")
endif ()


target_compile_definitions(${TARGET_NAME}
    PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
)

target_link_libraries(${TARGET_NAME} PUBLIC
    Tetragrama
    imported::External_obeliskLibs
)

set_target_properties(${TARGET_NAME} PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)

install(TARGETS ${TARGET_NAME} DESTINATION bin)
