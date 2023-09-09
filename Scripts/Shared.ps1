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

function CompareVersion([System.Version] $Version, [string] $ExpectedVersion) {
    $Version.CompareTo([System.Version]::new($ExpectedVersion)) -ge 0
}

function Find-CMake () {

    [string]$CMakeMinimumVersion = "3.20"

    $InstalledCMakeProgram = @(
        'cmake'
        if($IsWindows){
            Join-Path -Path $env:ProgramFiles -ChildPath 'CMake\bin\cmake.exe'
        }
    )

    foreach ($CMakeProgram in $InstalledCMakeProgram) {
        $Command = Get-Command $CMakeProgram -ErrorAction SilentlyContinue
        if($Command) {
            if($IsWindows -and (CompareVersion $Command.Version $CMakeMinimumVersion )){
                return $Command.Source
            }

            if ((& $Command --version | Out-String) -match "cmake version ([\d\.]*)") {
                [Version] $CMakeVersion = $Matches[1]
                if (CompareVersion $CMakeVersion $CMakeMinimumVersion) {
                    return $Command.Source
                }
            }
        }
    }

    throw "Failed to find cmake. Tried: " + ($CMakeCandidates -join ', ')
}

function Find-GLSLC () {

    [string]$GLSLCMinimumVersion = "11.1.0"

    $GLSLCCandidates = @(
        'glslc'
        if($IsWindows) {
            Join-Path -Path $env:VULKAN_SDK -ChildPath "\bin\glslc.exe"
        }
    )

    foreach ($GLSLCProgram in $GLSLCCandidates) {
        $Command = Get-Command $GLSLCProgram -ErrorAction SilentlyContinue
        if ($Command) {
            if ((& $Command --version | Out-String) -match "glslang ([\d\.]*)") {
                [Version] $GLSLCVersion = $Matches[1]
                if (CompareVersion $GLSLCVersion $GLSLCMinimumVersion) {
                    return $Command.Source
                }
            }
        }
    }

    throw "Failed to find glslc. Tried: " + ($GLSLCCandidates -join ', ')
}