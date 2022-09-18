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
    [Parameter(HelpMessage="System name to build, default to all")]
    [ValidateSet('Windows', 'Linux', 'Darwin')]
    [string[]] $SystemNames = @('Windows', 'Linux', 'Darwin'),

    [Parameter(HelpMessage="Architecture type to build, default to x64")]
    [ValidateSet('x64')]
    [string] $Architectures = 'x64',

    [Parameter(HelpMessage="Configuration type to build")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = @('Debug', 'Release'),

    [Parameter(HelpMessage="Whether to run build, default to True")]
    [bool] $RunBuilds = $True
)

$ErrorActionPreference = "Stop"
[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")

. (Join-Path $PSScriptRoot Shared.ps1)

$CMakeProgram  =  Find-CMake
if($CMakeProgram) {
    Write-Host "CMake program found..."
}

function Build([string]$systemName, [string]$architecture, [string]$configuration, [bool]$runBuild) {
    $IsMultipleConfig = ($systemName -eq 'Windows')

    Write-Host "Building $systemName $architecture $configuration"

    [string]$BuildDirectoryNameExtension = If($IsMultipleConfig) { "MultiConfig" } Else { $configuration }
    [string]$BuildDirectoryName = "Result." + $systemName + "." + $architecture + "." + $BuildDirectoryNameExtension
    [string]$BuildDirectoryPath = [IO.Path]::Combine($RepoRoot, $BuildDirectoryName)
    [string]$CMakeCacheVariableOverride = ""
    [string]$CMakeGenerator = ""

    # Create build directory
    #
    if(-Not (Test-Path $BuildDirectoryPath)) {
        $createDir = mkdir $BuildDirectoryPath
    }

    # Define CMake build arguments
    #
    if($systemName -eq "Windows") {
        $CMakeGenerator = "-G `"Visual Studio 17 2022`" -A $architecture"
        $CMakeCacheVariableOverride += " -DCMAKE_CONFIGURATION_TYPES=Debug;Release"
    }
    elseif ($systemName -eq "Darwin") {
        $CMakeGenerator = "-G `"Xcode`""
    }
    else {
        $CMakeGenerator = "-G `"Unix Makefiles`""
    }

    $CMakeCacheVariableOverride += " -DCMAKE_SYSTEM_NAME=$systemName"
    $CMakeCacheVariableOverride += " -DCMAKE_BUILD_TYPE=$configuration"
    $CMakeCacheVariableOverride += " -DBUILD_SANDBOX_PROJECTS=ON"
    $CMakeCacheVariableOverride += " -DENTT_INCLUDE_HEADERS=ON"
    
    # SDL2 options
    #    
    if ($systemName -eq "Linux") {
        $CMakeCacheVariableOverride += " -DSDL_STATIC=ON"
        $CMakeCacheVariableOverride += " -DSDL_SHARED=OFF"
    }
    
    # Spdlog options
    #
    $CMakeCacheVariableOverride += " -DSPDLOG_BUILD_SHARED=OFF"
    $CMakeCacheVariableOverride += " -DBUILD_STATIC_LIBS=ON"
    $CMakeCacheVariableOverride += " -DSPDLOG_FMT_EXTERNAL=ON"
    $CMakeCacheVariableOverride += " -DSPDLOG_FMT_EXTERNAL_HO=OFF"
    
    # GLFW options
    #
    if ($systemName -ne "Linux") {
        $CMakeCacheVariableOverride += " -DGLFW_BUILD_DOCS=OFF"
        $CMakeCacheVariableOverride += " -DGLFW_BUILD_EXAMPLES=OFF"
        $CMakeCacheVariableOverride += " -DGLFW_INSTALL=OFF"
    }

    # ASSIMP options
    #
    $CMakeCacheVariableOverride += " -DASSIMP_BUILD_TESTS=OFF"
    $CMakeCacheVariableOverride += " -DASSIMP_INSTALL=OFF"
    $CMakeCacheVariableOverride += " -DASSIMP_BUILD_SAMPLES=OFF"
    $CMakeCacheVariableOverride += " -DASSIMP_BUILD_ASSIMP_TOOLS=OFF"
    
    if ($systemName -eq "Darwin") {
        $CMakeCacheVariableOverride += " -DBUILD_FRAMEWORK=ON"
    }

    # STDUUID options
    #
    $CMakeCacheVariableOverride += " -DUUID_BUILD_TESTS=OFF"
    $CMakeCacheVariableOverride += " -DUUID_USING_CXX20_SPAN=ON"

    $CMakeArguments = " -S $RepoRoot -B $BuildDirectoryPath $CMakeGenerator $CMakeCacheVariableOverride"

    # Set Linux build compiler
    #
    if($systemName -eq 'Linux') {
        $env:CC= '/usr/bin/gcc-11'
        $env:CXX= '/usr/bin/g++-11'
    }

    # CMake Generation process
    #
    Write-Host $CMakeArguments
    $process = Start-Process $CMakeProgram -ArgumentList $CMakeArguments -NoNewWindow -Wait -PassThru
    if($process.ExitCode -ne 0) {
        throw "cmake failed generation for '$argumentList' with exit code '$p.ExitCode'"
    }

    # CMake Build Process
    #
    if($runBuild) {
        if ($CMakeGenerator -like "Visual Studio*") {
            # With a Visual Studio Generator, `msbuild.exe` is used to run the build. By default, `msbuild.exe` will
            # launch worker processes to opportunistically re-use for subsequent builds. To cause the worker processes
            # to exit at the end of the main process, pass `-nodeReuse:false`.
            $buildToolOptions = '-nodeReuse:false'
        }

        $buildArguments = "--build $BuildDirectoryPath --config $configuration"
        if($buildToolOptions) {
            $buildArguments += " -- $buildToolOptions"
        }

        $process = Start-Process $CMakeProgram -ArgumentList $buildArguments -NoNewWindow -Wait -PassThru
        $process.WaitForExit();
        $exitCode = $process.ExitCode
        if ($exitCode -ne 0) {
            throw "cmake failed build for '$buildArguments' with exit code '$exitCode'"
        }
    }

}

foreach ($system in $SystemNames) {
    foreach ($arch in $Architectures) {
        foreach ($config in $Configurations) {
            Build $system $arch $config $RunBuilds
        }
    }
}