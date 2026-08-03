
# Installs a NuGet package
#
#   install_nuget_package(<nuget name> <nuget version> <path property>)
#
# The NuGet will be download during CMake generation.
#
function(install_nuget_package NUGET_PACKAGE_NAME NUGET_PACKAGE_VERSION NUGET_PATH_PROPERTY)
    #  NUGET_PACKAGE_NAME       - The name of the NuGet package.
    #  NUGET_PACKAGE_VERSION    - The version of the NuGet package.
    find_program(NUGET_EXECUTABLE nuget REQUIRED)

    if("${EXTERNAL_NUGET_DIR}" STREQUAL "")
        set(EXTERNAL_NUGET_DIR "${CMAKE_BINARY_DIR}/__packages")
    endif()

    cmake_parse_arguments("NUGET" "" "SOURCE" "" ${ARGN})

    if(NOT ("${NUGET_SOURCE}" STREQUAL ""))
        message(STATUS "Nuget source for ${NUGET_PACKAGE_NAME}@${NUGET_PACKAGE_VERSION} is ${NUGET_SOURCE}")
        set(NUGET_SOURCE_COMMAND -Source "${NUGET_SOURCE}")
    endif()

    # The NUGET_PACKAGE_ROOT is where NUGET_EXECUTABLE installed the package to.
    set(NUGET_PACKAGE_ROOT "${EXTERNAL_NUGET_DIR}/${NUGET_PACKAGE_NAME}.${NUGET_PACKAGE_VERSION}")

    if(NOT EXISTS "${NUGET_PACKAGE_ROOT}")
        message(STATUS "nuget install ${NUGET_PACKAGE_NAME} -Version ${NUGET_PACKAGE_VERSION} -OutputDirectory ${EXTERNAL_NUGET_DIR}")
        execute_process(
            COMMAND ${NUGET_EXECUTABLE} install ${NUGET_PACKAGE_NAME} -Version ${NUGET_PACKAGE_VERSION} -OutputDirectory "${EXTERNAL_NUGET_DIR}" ${NUGET_SOURCE_COMMAND}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE NUGET_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()

    set(${NUGET_PATH_PROPERTY} "${NUGET_PACKAGE_ROOT}" PARENT_SCOPE)
endfunction()

function(restore_nuget_packages VS_SLN_OR_PROJ)
    #  VS_SLN_OR_PROJ - the Visual Studio .sln, .vcxproj or .csproj defining the nuget dependencies

    find_program(NUGET_EXECUTABLE nuget REQUIRED)

    message(STATUS "NuGet restore: ${VS_SLN_OR_PROJ}")

    execute_process(
        COMMAND ${NUGET_EXECUTABLE} restore ${VS_SLN_OR_PROJ}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NUGET_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endfunction()
