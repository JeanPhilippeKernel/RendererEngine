cmake_minimum_required (VERSION 3.17)

project (Panzerfaust
	VERSION 1.0
	DESCRIPTION "Panzerfaust, the ZEngine Launcher"
)

if ((APPLE) OR (CMAKE_GENERATOR STREQUAL "Visual Studio 17 2022"))
    set(CSPROJ Panzerfaust)

    restore_nuget_packages(
        ${CMAKE_CURRENT_LIST_DIR}/${CSPROJ}.csproj)

    include_external_msproject(
        ${CSPROJ}
        ${CMAKE_CURRENT_LIST_DIR}/${CSPROJ}.csproj
        PLATFORM "x64"
        TYPE "4301B74A-9ED7-48ED-9DE5-FC2E1457778D"
        GUID "FAE04EC0-301F-11D3-BF4B-00C04F79EFBC"
    )
endif()

