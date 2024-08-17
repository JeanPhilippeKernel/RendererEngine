cmake_minimum_required (VERSION 3.17)

project (Panzerfaust
	VERSION 1.0
	DESCRIPTION "Panzerfaust, the ZEngine Launcher"
)

set(CSPROJ Panzerfaust)
set (CSPROJ_SOURCE_FILE ${CMAKE_CURRENT_LIST_DIR}/${CSPROJ}.csproj)

if ((APPLE) OR (CMAKE_GENERATOR STREQUAL "Visual Studio 17 2022"))

    restore_nuget_packages(${CSPROJ_SOURCE_FILE})

    set (Platform "x64")
    if (APPLE AND MACOSX_ARCHITECTURE_ARM64)
        set (Platform "arm64")
    endif ()
    include_external_msproject(
        ${CSPROJ}
        ${CSPROJ_SOURCE_FILE}
        PLATFORM ${Platform}
        TYPE "4301B74A-9ED7-48ED-9DE5-FC2E1457778D"
        GUID "FAE04EC0-301F-11D3-BF4B-00C04F79EFBC"
    )
endif()

set (BUILD_CONFIGURATION -c $<IF:$<CONFIG:Debug>,Debug,Release>)
if (APPLE)
    set (RUNTIME_IDENTIFIER -r $<IF:$<BOOL:${MACOSX_ARCHITECTURE_ARM64}>,osx-arm64,osx-x64>)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set (RUNTIME_IDENTIFIER -r win-x64)
endif ()

add_custom_target (BuildLauncher ALL
    COMMENT "Building and Publishing Panzerfaust"
    COMMAND dotnet build ${CSPROJ_SOURCE_FILE} ${RUNTIME_IDENTIFIER} ${BUILD_CONFIGURATION} -v n
    COMMAND dotnet publish ${CSPROJ_SOURCE_FILE} ${RUNTIME_IDENTIFIER} ${BUILD_CONFIGURATION}
    DEPENDS ${BUILD_LAUNCHER_DEPENDS_LIB}
    VERBATIM
)

if (NOT LAUNCHER_ONLY)
    add_dependencies(BuildLauncher zEngineLib tetragrama)
endif ()