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

Set-Variable CMAKE_GENERATOR_DARWIN -Option None -value 'Xcode'
Set-Variable CMAKE_GENERATOR_UNIX -Option None -value 'Unix Makefiles'

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
          
            $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,'DCMAKE_CONFIGURATION_TYPES=Debug;Release' -join " -"
         }

         "Linux" { 
            $CMakeGenerator = "-G `"$CMAKE_GENERATOR_UNIX`""
            $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,'DSDL_STATIC=ON','DSDL_SHARED=OFF' -join " -"

            #Set Linux build compiler
            $env:CC= '/usr/bin/gcc-11'
            $env:CXX= '/usr/bin/g++-11'
        }
        "Darwin" { 
            $CMakeGenerator = "-G `"$CMAKE_GENERATOR_DARWIN`""
            $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,'DBUILD_FRAMEWORK=ON' -join " -"
        }
        Default {
            throw 'This system is not supported'
        }
    }

    $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,('DCMAKE_SYSTEM_NAME='+$SystemName),
    ('DCMAKE_BUILD_TYPE='+$configuration),'DBUILD_SANDBOX_PROJECTS=ON','DENTT_INCLUDE_HEADERS=ON','DSPDLOG_BUILD_SHARED=OFF','DBUILD_STATIC_LIBS=ON','DSPDLOG_FMT_EXTERNAL=ON','DSPDLOG_FMT_EXTERNAL_HO=OFF','DASSIMP_BUILD_TESTS=OFF','DASSIMP_INSTALL=OFF','DASSIMP_BUILD_SAMPLES=OFF','DASSIMP_BUILD_ASSIMP_TOOLS=OFF','DUUID_BUILD_TESTS=OFF','DUUID_USING_CXX20_SPAN=ON' -join " -"

    if(!$IsLinux){
        $CMakeCacheVariableOverride = $CMakeCacheVariableOverride,'DGLFW_BUILD_DOCS=OFF','DGLFW_BUILD_EXAMPLES=OFF','DGLFW_INSTALL=OFF' -join " -"
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
