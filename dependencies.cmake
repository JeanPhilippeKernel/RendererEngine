if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    # Install necessary NuGet dependencies
    install_nuget_package(Microsoft.Windows.CppWinRT 2.0.240405.15 CPPWINRT_NUGET_PATH)

    # Generate CppWinRT headers for the local OS
    generate_winrt_headers(
        EXECUTABLE
            ${CPPWINRT_NUGET_PATH}/bin/cppwinrt.exe
        INPUT
            local
        OUTPUT
            ${CMAKE_BINARY_DIR}/__winrt
        OPTIMIZE
    )

    add_library(imported::cppwinrt_headers INTERFACE IMPORTED)
    target_include_directories(imported::cppwinrt_headers INTERFACE
        ${CMAKE_BINARY_DIR}/__winrt
    )
endif()