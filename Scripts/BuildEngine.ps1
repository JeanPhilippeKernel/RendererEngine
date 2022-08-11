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
    [Parameter(HelpMessage = "configuration type to build")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = @('Debug', 'Release'),

    [Parameter(HelpMessage = "Whether to run build, default to True")]
    [bool] $RunBuilds = $True,

    [Parameter(HelpMessage = "VS version use to build, default to 2019")]
    [ValidateSet(2019, 2022)]
    [int] $VsVersion = 2019
)

$ErrorActionPreference = "Stop"
[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")

. (Join-Path $PSScriptRoot Shared.ps1)

$cMakeProgram = Find-CMake
if ($cMakeProgram) {
    Write-Host "CMake program found..."
}
else {
    throw 'CMake program not found'
}

function Build([string]$configuration, [int]$VsVersion , [bool]$runBuild) {
    
    $architecture = 'x64'

    # Check if the system supports multiple configurations
    #
    $isMultipleConfig = $IsWindows

    # Check System name
    if ($IsLinux) {
        $systemName = "Linux"
        $cMakeGenerator 
    }
    elseif ($IsMacOS) {
        $systemName = "Darwin"
    }
    elseif ($IsWindows) {
        $systemName = "Windows"
    }
    else {
        throw 'The OS is not supported' 
    }

    Write-Host "Building $systemName $architecture $configuration"

    [string]$BuildDirectoryNameExtension = If ($isMultipleConfig) { "MultiConfig" } Else { $configuration }
    [string]$BuildDirectoryName = "Result." + $systemName + "." + $architecture + "." + $BuildDirectoryNameExtension
    [string]$buildDirectoryPath = [IO.Path]::Combine($RepoRoot, $BuildDirectoryName)
    [string]$cMakeCacheVariableOverride = ""
    [string]$cMakeGenerator = ""

    # Create build directory
    if (-Not (Test-Path $buildDirectoryPath)) {
        $Null = New-Item -ItemType Directory -Path $BuildDirectoryPath -ErrorAction SilentlyContinue
    } 

    $cMakeOptions = " -DCMAKE_SYSTEM_NAME=$systemName", " -DCMAKE_BUILD_TYPE=$configuration"
    $submoduleCMakeOptions = @{
        'ENTT'= @("-DENTT_INCLUDE_HEADERS=ON")
        'SDL2' = @("-DSDL_STATIC=ON", "-DSDL_SHARED=OFF");
        'SPDLOG' = @("-DSPDLOG_BUILD_SHARED=OFF", "-DBUILD_STATIC_LIBS=ON", "-DSPDLOG_FMT_EXTERNAL=ON", "-DSPDLOG_FMT_EXTERNAL_HO=OFF");
        'GLFW '= @("-DGLFW_BUILD_DOCS=OFF", "-DGLFW_BUILD_EXAMPLES=OFF", "-DGLFW_INSTALL=OFF");
        'ASSIMP'=@("-DASSIMP_BUILD_TESTS=OFF", "-DASSIMP_INSTALL=OFF", "-DASSIMP_BUILD_SAMPLES=OFF", "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF");
        'STDUUID'=@("-DUUID_BUILD_TESTS=OFF", "-DUUID_USING_CXX20_SPAN=ON");
        'FRAMEWORK'=@("-DBUILD_FRAMEWORK=ON");
    }  

    $cMakeCacheVariableOverride = $cMakeOptions -join ' ' 

    # Building CMake arguments switch to System
    switch ($systemName) {
        "Windows" { 
            switch ($VsVersion) {
                2019 { 
                    $cMakeGenerator = "-G `"Visual Studio 16 2019`" -A $architecture"
                }
                2022 { 
                    $cMakeGenerator = "-G `"Visual Studio 17 2022`" -A $architecture"
                }
                Default {
                    throw 'This version of Visual Studio is not supported' 
                }
            }          
            $cMakeCacheVariableOverride += ' -DCMAKE_CONFIGURATION_TYPES=Debug;Release '
        }
        "Linux" { 
            $cMakeGenerator = "-G `"Unix Makefiles`""
            $cMakeCacheVariableOverride += $submoduleCMakeOptions.SDL2 -join ' ' 

            # Set Linux build compiler
            $env:CC = '/usr/bin/gcc-11'
            $env:CXX = '/usr/bin/g++-11'
        }
        "Darwin" { 
            $cMakeGenerator = "-G `"Xcode`""
            $cMakeCacheVariableOverride += $submoduleCMakeOptions.FRAMEWORK -join ' ' 
        }
        Default {
            throw 'This system is not supported'
        }
    }

    $cMakeCacheVariableOverride += ' ' + $submoduleCMakeOptions.ENT -join ' ' 
    $cMakeCacheVariableOverride += ' ' + $submoduleCMakeOptions.SPDLOG -join ' ' 
    $cMakeCacheVariableOverride += ' ' + $submoduleCMakeOptions.ASSIMP -join ' ' 
    $cMakeCacheVariableOverride += ' ' + $submoduleCMakeOptions.STDUUID -join ' ' 

    if (-not $IsLinux) {
        $cMakeCacheVariableOverride += ' ' + $submoduleCMakeOptions.GLFW -join ' '
    }

    $cMakeArguments = " -S $RepoRoot -B $buildDirectoryPath $cMakeGenerator $cMakeCacheVariableOverride"   

    # CMake Generation process
    Write-Host $cMakeArguments
    $cMakeProcess = Start-Process $cMakeProgram -ArgumentList $cMakeArguments -NoNewWindow -Wait -PassThru   
    if ($cMakeProcess.ExitCode -ne 0 ) {
        throw "cmake failed generation for '$cMakeArguments' with exit code '$cMakeProcess.ExitCode'"
    }

    # CMake Build Process
    #
    if ($runBuild) {
        if ($cMakeGenerator -like 'Visual Studio*') {
            # With a Visual Studio Generator, `msbuild.exe` is used to run the build. By default, `msbuild.exe` will
            # launch worker processes to opportunistically re-use for subsequent builds. To cause the worker processes
            # to exit at the end of the main process, pass `-nodeReuse:false`.
            $buildToolOptions = '-nodeReuse:false'
        }

        $buildArguments = "--build $buildDirectoryPath --config $configuration"
        if ($buildToolOptions) {
            $buildArguments = $buildArguments, $buildToolOptions -join " --"
        }

        $buildProcess = Start-Process $cMakeProgram -ArgumentList $buildArguments -NoNewWindow -Wait -PassThru
        $buildProcess.WaitForExit();
        if ($buildProcess.ExitCode -ne 0) {
            throw "cmake failed build for '$buildArguments' with exit code '$buildProcess.ExitCode'"
        }
    }
}

foreach ($config in $Configurations) {
    Build $config $VsVersion $RunBuilds 
}
