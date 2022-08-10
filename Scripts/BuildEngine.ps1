# MIT License

# Copyright (c) 2020 Jean Philippe

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


#Requires -PSEdition Core

param (
    [Parameter(HelpMessage="Configuration type to build")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = @('Debug', 'Release'),

    [Parameter(HelpMessage="Whether to run build, default to True")]
    [bool] $RunBuilds = $True,

    [Parameter(HelpMessage="VS version use to build, default to 2019")]
    [ValidateSet(2019, 2022)]
    [int] $VSVersion = 2019
)


##Define None variables
Set-Variable ARCHITECTURE -Option None -value 'x64'
Set-Variable SYSTEM_NAMES -Option None -value 'Windows', 'Linux', 'Darwin'

# # CMake arguments variables
Set-Variable CMAKE_CONFIGURATION_TYPES -Option None -value 'DCMAKE_CONFIGURATION_TYPES=Debug;Release'
Set-Variable CMAKE_GENERATOR_DARWIN -Option None -value 'Xcode'
Set-Variable CMAKE_GENERATOR_UNIX -Option None -value 'Unix Makefiles'

Set-Variable CMAKE_SYSTEM_NAME -Option None -value 'DCMAKE_SYSTEM_NAME='
Set-Variable CMAKE_BUILD_TYPE -Option None -value 'DCMAKE_BUILD_TYPE='
Set-Variable BUILD_SANDBOX_PROJECTS -Option None -value 'DBUILD_SANDBOX_PROJECTS=ON'
Set-Variable ENTT_INCLUDE_HEADERS -Option None -value 'DENTT_INCLUDE_HEADERS=ON'

# # SDL2 options variables
Set-Variable SDL_STATIC -Option None -value 'DSDL_STATIC=ON'
Set-Variable SDL_SHARED -Option None -value 'DSDL_SHARED=OFF'

# # SPDLOG options variables
Set-Variable SPDLOG_BUILD_SHARED -Option None -value 'DSPDLOG_BUILD_SHARED=OFF'
Set-Variable BUILD_STATIC_LIBS -Option None -value 'DBUILD_STATIC_LIBS=ON'
Set-Variable SPDLOG_FMT_EXTERNAL -Option None -value 'DSPDLOG_FMT_EXTERNAL=ON'
Set-Variable SPDLOG_FMT_EXTERNAL_HO -Option None -value 'DSPDLOG_FMT_EXTERNAL_HO=OFF'

# # GLFW options variables
Set-Variable GLFW_BUILD_DOCS -Option None -value 'DGLFW_BUILD_DOCS=OFF'
Set-Variable GLFW_BUILD_EXAMPLE -Option None -value 'DGLFW_BUILD_EXAMPLES=OFF'
Set-Variable GLFW_INSTALL -Option None -value 'DGLFW_INSTALL=OFF'

# # ASSIMP options variables
Set-Variable ASSIMP_BUILD_TESTS -Option None -value 'DASSIMP_BUILD_TESTS=OFF'
Set-Variable ASSIMP_INSTALL -Option None -value 'DASSIMP_INSTALL=OFF'
Set-Variable ASSIMP_BUILD_SAMPLES -Option None -value 'DASSIMP_BUILD_SAMPLES=OFF'
Set-Variable ASSIMP_BUILD_ASSIMP_TOOLS -Option None -value 'DASSIMP_BUILD_ASSIMP_TOOLS=OFF'

# # FRAMEWORK options variables
Set-Variable BUILD_FRAMEWORK -Option None -value 'DBUILD_FRAMEWORK=ON'

# # STDUUID options variables
Set-Variable UUID_BUILD_TESTS -Option None -value 'DUUID_BUILD_TESTS=OFF'
Set-Variable UUID_USING_CXX20_SPAN -Option None -value 'DUUID_USING_CXX20_SPAN=ON'

# # Linux build compiler options variables
Set-Variable ENV_CC -Option None -value '/usr/bin/gcc-11'
Set-Variable ENV_CXX -Option None -value '/usr/bin/g++-11'

# VS Version supported
Set-Variable VS16 -Option None -value 'Visual Studio 16 2019'
Set-Variable VS17 -Option None -value 'Visual Studio 17 2022'


$ErrorActionPreference = "Stop"
[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")

. (Join-Path $PSScriptRoot Shared.ps1)

$CMakeProgram  =  Find-CMake

if($CMakeProgram) {
    Write-Host "CMake program found..."
}else{
    throw 'CMake program not found'
    exit(1)
}

function Build([string]$Configuration ="Debug", [int]$VSVersion = 2019 ,[bool]$RunBuild =$True) {
    
    #Check if the OS allow multiple configguration, if not windows this value will'be false
    $IsMultipleConfig = $IsWindows

    #Check System name
    if ($IsLinux) {
        $SystemName =  "Linux"
    }
    elseif ($IsMacOS) {
        $SystemName =   "Darwin"
    }
    elseif ($IsWindows) {
        $SystemName =  "Windows"
    }else{
        throw 'The OS is not supported' 
    }


    Write-Host "Building $SystemName $Configuration"

    [string]$BuildDirectoryNameExtension = If($IsMultipleConfig) { "MultiConfig" } Else { $Configuration }
    [string]$BuildDirectoryName = "Result." + $SystemName + "." + $ARCHITECTURE + "." + $BuildDirectoryNameExtension
    [string]$BuildDirectoryPath = [IO.Path]::Combine($RepoRoot, $BuildDirectoryName)
    [string]$CMakeCacheVariableOverride = ""
    [string]$CMakeGenerator = ""

    # Create build directory
    #
    if(-Not (Test-Path $BuildDirectoryPath)) {
        mkdir $BuildDirectoryPath
    }
    # else{
    #     #Clear folder if it's not empty before
    #     $BuildDirectoryPathInfo = Get-ChildItem $BuildDirectoryPath | Measure-Object
    #     if($BuildDirectoryPathInfo.Count -ne 0){
    #         Get-ChildItem $BuildDirectoryPath -Recurse  | Get-ChildItem | Remove-Item -Recurse -Force
    #     }
    # }


    #Building CMake arguments switch to System

    switch ($SystemName) {
        "Windows" { 
            switch ($VSVersion) {
                2019 { 
                    $CMakeGenerator = "-G `"$VS16`" -A $ARCHITECTURE"
                 }
                2022 { 
                    $CMakeGenerator = "-G `"$VS17`" -A $ARCHITECTURE"
                 }
                Default {
                    throw 'This version of Visual Studio is not supported' 
                }
            }
            # $CMakeGenerator = "-G `"Visual Studio 16 2019`" -A $ARCHITECTURE"
            $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,$CMAKE_CONFIGURATION_TYPES -join " -"
         }

         "Linux" { 
            $CMakeGenerator = "-G `"$CMAKE_GENERATOR_UNIX`""
            $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,$SDL_STATIC,$SDL_SHARED -join " -"

            #Set Linux build compiler
            $env:CC= $ENV_CC
            $env:CXX= $ENV_CXX
        }
        "Darwin" { 
            $CMakeGenerator = "-G `"$CMAKE_GENERATOR_DARWIN`""
            $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,$BUILD_FRAMEWORK -join " -"
        }
        Default {
            throw 'This system is not supported'
        }
    }

    $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,($CMAKE_SYSTEM_NAME+$SystemName),
    ($CMAKE_BUILD_TYPE+$configuration),$BUILD_SANDBOX_PROJECTS,$ENTT_INCLUDE_HEADERS,$SPDLOG_BUILD_SHARED,$BUILD_STATIC_LIBS,$SPDLOG_FMT_EXTERNAL,$SPDLOG_FMT_EXTERNAL_HO,$ASSIMP_BUILD_TESTS,$ASSIMP_INSTALL,$ASSIMP_BUILD_SAMPLES,$ASSIMP_BUILD_ASSIMP_TOOLS,$UUID_BUILD_TESTS,$UUID_USING_CXX20_SPAN,$ASSIMP_BUILD_TESTS,$ASSIMP_INSTALL,$ASSIMP_BUILD_SAMPLES,$ASSIMP_BUILD_ASSIMP_TOOLS -join " -"

    if(!$IsLinux){
        $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,$GLFW_BUILD_DOCS,$GLFW_BUILD_EXAMPLE,$GLFW_INSTALL -join " -"
    }

    $CMakeArguments = " -S $RepoRoot -B $BuildDirectoryPath $CMakeGenerator $CMakeCacheVariableOverride"
   

    # CMake Generation process
    #
    Write-Host $CMakeArguments
    $CMakeProcess = Start-Process $CMakeProgram -ArgumentList $CMakeArguments -NoNewWindow -Wait -PassThru
   
    if ($CMakeProcess.ExitCode -ne 0 ){
        throw "cmake failed generation for '$CMakeArguments' with exit code '$CMakeProcess.ExitCode'"
    }

    # CMake build processing
    if($RunBuild){
        if($CMakeGenerator -like 'Visual Studio*'){
             # With a Visual Studio Generator, `msbuild.exe` is used to run the build. By default, `msbuild.exe` will
            # launch worker processes to opportunistically re-use for subsequent builds. To cause the worker processes
            # to exit at the end of the main process, pass `-nodeReuse:false`.
            $BuildToolOptions = '-nodeReuse:false'
        }

        $BuildArguments = "--build $BuildDirectoryPath --config $Configuration"

        if($BuildToolOptions){
            $BuildArguments = $BuildArguments,$BuildToolOptions -join " --"
        }

        $BuildProcess = Start-Process $CMakeProgram -ArgumentList $BuildArguments -NoNewWindow -Wait -PassThru

        $BuildProcess.WaitForExit();

        if($BuildProcess.ExitCode -ne 0){
            throw "cmake failed build for '$BuildArguments' with exit code '$BuildProcess.ExitCode'"
        }
    }
}

        foreach ($config in $Configurations) {
            Build $config $VSVersion $RunBuilds 
        }
