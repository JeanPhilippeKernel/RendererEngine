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

function Get-RepositoryToolPath () {
    $toolPath = Join-Path $repositoryRootPath -ChildPath "__tools"

    if (-not (Test-Path $toolPath)) {
        $Null = New-Item -ItemType Directory -Path $toolPath -ErrorAction SilentlyContinue
    }

    return $toolPath
}

function Find-Nuget () {

    $repoConfiguration = Get-RepositoryConfiguration
    $NugetMinimumVersion = $repoConfiguration.Requirements.Nuget.Version

    $NugetProgramCandidates = @(
        'nuget' 
        Join-Path -Path $repoConfiguration.Paths.Tools -ChildPath 'NuGet\nuget'
        if ($IsWindows) {
          Join-Path -Path $repoConfiguration.Paths.Tools -ChildPath 'NuGet\nuget.exe'
        }
    )

    foreach ($NugetProgram in $NugetProgramCandidates) {
        $Command = Get-Command $NugetProgram -ErrorAction SilentlyContinue
        if ($Command) {
            if ((& $Command help -ForceEnglishOutput | Select-String -Pattern "NuGet Version" | Out-String) -match "NuGet Version: ([\d\.]*)") {
                [Version] $NugetVersion = $Matches[1]
                if (CompareVersion $NugetVersion $NugetMinimumVersion) {
                    return $Command.Source
                }
            }
        }
    }
}

function Setup-NuGet {
    $installPath = Join-Path -Path (Get-RepositoryToolPath) -ChildPath "NuGet"
    $nugetPath = Join-Path -Path $installPath -ChildPath "nuget.exe"

    if (-not (Test-Path $installPath)) { 
        New-Item -ItemType Directory -Path $installPath | Out-Null
    }

    $repoConfiguration = Get-RepositoryConfiguration
    $nugetUrl = $repoConfiguration.Requirements.Nuget.Url
    Invoke-WebRequest -Uri $nugetUrl -OutFile $nugetPath

    Write-Host " Nuget Installation and configuration completed successfully!"
}

function Get-RepositoryConfiguration () {
    $repoConfigFile = Join-Path $repositoryRootPath 'repoConfiguration.json'

    $configuration = Get-Content $repoConfigFile | ConvertFrom-Json
    $repositoryToolPath = Get-RepositoryToolPath
    $paths = New-Object -TypeName PSCustomObject -Property @{
        Root  = $repositoryRootPath
        Tools = $repositoryToolPath
    }
    $configuration | Add-Member -MemberType NoteProperty -Name 'Paths' -Value $paths

    return $configuration
}

function Find-CMake () {

    $repoConfiguration = Get-RepositoryConfiguration
    $CMakeMinimumVersion = $repoConfiguration.Requirements.CMake.Version

    $CMakeProgramCandidates = @(
        'cmake'
        if ($IsWindows) {
            Join-Path -Path $env:ProgramFiles -ChildPath 'CMake\bin\cmake.exe'
        }
    )

    foreach ($CMakeProgram in $CMakeProgramCandidates) {
        $Command = Get-Command $CMakeProgram -ErrorAction SilentlyContinue
        if ($Command) {
            if ($IsWindows -and (CompareVersion $Command.Version $CMakeMinimumVersion )) {
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

    throw "Failed to find cmake. Tried: " + ($CMakeProgramCandidates -join ', ')
}

function Find-GLSLC () {

    $repoConfiguration = Get-RepositoryConfiguration
    $GLSLCMinimumVersion = $repoConfiguration.Requirements.ShaderC.MinimumVersion;

    $shaderCCompilerPath = Setup-ShaderCCompilerTool

    $GLSLCCandidates = @(
        'glslc'
        if ($IsWindows) {
            Join-Path -Path $shaderCCompilerPath -ChildPath "\bin\glslc.exe" # On Windows, the pipeline build might pick up this option...
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

function Find-GlslangValidator () {

    $repoConfiguration = Get-RepositoryConfiguration
    $GlslangValidatorVersion = $repoConfiguration.Requirements.GlslangValidator.Version;

    $shaderCCompilerPath = Setup-ShaderCCompilerTool

    $GlslangValidatorCandidates = @(
        'glslangValidator'
        if ($IsWindows) {
            Join-Path -Path $shaderCCompilerPath -ChildPath "\bin\glslangValidator.exe" # On Windows, the pipeline build might pick up this option...
        }
        if ($IsMacOS) {
            Join-Path -Path $shaderCCompilerPath -ChildPath "\bin\glslangValidator" # On macOS, the pipeline build might pick up this option...
        }
    )

    foreach ($GlslangValidatorProgram in $GlslangValidatorCandidates) {
        $Command = Get-Command $GlslangValidatorProgram -ErrorAction SilentlyContinue
        if ($Command) {
            if ((& $Command --version | Out-String) -match "glslang Khronos. ([\d\.]*)") {
                [Version] $FoundVersion = $Matches[1]
                if (CompareVersion $FoundVersion $GlslangValidatorVersion) {
                    return $Command.Source
                }
            }
        }
    }

    throw "Failed to find glslangValidator. Tried: " + ($GlslangValidatorCandidates -join ', ')
}

function Find-ClangFormat () {
    $repoConfiguration = Get-RepositoryConfiguration
    $LLVMMinimumVersion = $repoConfiguration.Requirements.LLVM.Version
    $LLVMMaximumVersion = $repoConfiguration.Requirements.LLVM.MaximumVersion

    $candidates = @(
        'clang-format'
        if ($IsMacOS) {
            $brewPrefixPath = Invoke-Expression -Command "& brew --prefix"
            Join-Path $brewPrefixPath -ChildPath 'opt/llvm/bin/clang-format'
        }
        if ($IsWindows) {
            Join-Path -Path $env:ProgramFiles -ChildPath 'LLVM\bin\clang-format.exe'
        }
    )

    foreach ($candidate in $candidates) {
        $clangFormatCommand = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($clangFormatCommand) {
            if ((& $clangFormatCommand --version | Out-String) -match "clang-format version ([\d\.]*)") {
                [Version] $clangFormatVersion = $Matches[1]
                if ((CompareVersion $clangFormatVersion $LLVMMinimumVersion) -and (CompareVersion $LLVMMaximumVersion $clangFormatVersion)) {
                    return $clangFormatCommand.Source
                }
            }
        }
    }

    throw "Failed to find clang-format min $LLVMMinimumVersion max $LLVMMaximumVersion. Tried: " + ($candidates -join ', ')
}

function Setup-ShaderCCompilerTool () {
    $repoConfiguration = Get-RepositoryConfiguration
    $repositoryToolPath = $repoConfiguration.Paths.Tools

    $shaderCToolPath = Join-Path "$repositoryToolPath" -ChildPath "ShaderCCompiler"
    
    if (-not (Test-Path $shaderCToolPath)) {
        $outputFileExtension = if($IsWindows) {".zip"} Else {".tgz"}
        $outputFile = Join-Path -Path $repositoryToolPath -ChildPath "ShaderCCompiler$outputFileExtension"
        Write-Host "Downloading Shader Compiler Tools..."

        $shaderCToolUrl = IF($IsWindows) {$repoConfiguration.Requirements.ShaderC.Windows.Url} Elseif($IsMacOS) {$repoConfiguration.Requirements.ShaderC.macOS.Url}
        Invoke-WebRequest -Uri $shaderCToolUrl -OutFile $outputFile

        # Extract contents
        if($IsWindows) {
            Expand-Archive -Path $outputFile  -DestinationPath $repositoryToolPath
        }
        else {
            [string[]] $unzipArguments = '-xvzf', $outputFile, '-C', $repositoryToolPath
            & tar $unzipArguments
        }

        # Reorganize contents
        $oldPathName = Join-Path $repositoryToolPath -ChildPath "install"
        $newPathName = Join-Path $repositoryToolPath -ChildPath "ShaderCCompiler"
        Rename-Item -Path $oldPathName -NewName $newPathName -Force
        Remove-Item $outputFile -Force
    }

    return $shaderCToolPath
}

function Get-RepositoryRootPath () {
    Push-Location $PSScriptRoot
    $rootPath = & git rev-parse --show-toplevel
    Pop-Location
    return $rootPath
}

$repositoryRootPath = Get-RepositoryRootPath
