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
