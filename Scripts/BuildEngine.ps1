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
    [Parameter(HelpMessage = "Configuration type to build")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = @('Debug', 'Release'),

    [Parameter(HelpMessage = "Architecture to build")]
    [ValidateSet('x64', 'arm64')]
    [string] $Architecture = 'x64',

    [Parameter(HelpMessage = "Whether to run build, default to True")]
    [bool] $RunBuilds = $True,

    [Parameter(HelpMessage = "Whether to run clang format to format the code, default to True")]
    [bool] $RunClangFormat = $True,

    [Parameter(HelpMessage = "Whether to check code formatting correctness, default to False")]
    [bool] $VerifyFormatting = $False,

    [Parameter(HelpMessage = "VS version use to build, default to 2022")]
    [ValidateSet('2022', '2026')]
    [int] $VsVersion = 2022,

    [Parameter(HelpMessage = "Build Launcher only")]
    [switch] $LauncherOnly
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot Shared.ps1)

$cMakeProgram = Find-CMake
if ($cMakeProgram) {
    Write-Host "CMake program found..."
}
else {
    throw 'CMake program not found'
}

if ($IsWindows) {
    $nugetProgram = Find-NuGet
    if ($nugetProgram) {
        Write-Host "NuGet program found at: $nugetProgram"
    }
    else {
        Write-Warning "NuGet program not found. Attempting to download and install NuGet..."
        Setup-NuGet

        $nugetProgram = Find-NuGet
        if ($nugetProgram) {
            Write-Host "NuGet installed successfully at: $nugetProgram"
        }
        else {
            throw 'Nuget program not found'
        }
    }

    #Add NuGet to the PATH for the current session if it's not already there
    $installPath = Split-Path -Path $nugetProgram -Parent
    if ($env:PATH -notlike "*$installPath*") {
        $env:PATH = "$installPath;$env:PATH"
    }
}


function Build([string]$configuration, [int]$VsVersion , [bool]$runBuild) {

    # Check if the system supports multiple configurations
    $isMultipleConfig = $IsWindows

    # Check the system name
    if ($IsLinux) {
        $systemName = "Linux"
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

    Write-Host "Configuring $systemName $architecture $configuration"

    [string]$cMakeCacheVariableOverride = ""

    $submoduleCMakeOptions = @{
        'LAUNCHER_ONLY' = @("-DLAUNCHER_ONLY=ON")
    }

    if($LauncherOnly) {
        $cMakeCacheVariableOverride += ' ' + $submoduleCMakeOptions.LAUNCHER_ONLY -join ' '
    } 

    # Define CMake Generator arguments
    $configName = $systemName, $architecture, $configuration -join "_"

    if($IsWindows){
        $configName += '_'+$VsVersion
    }

    $cMakeArguments = " --preset $configName $cMakeCacheVariableOverride"

    # CMake Generation process
    Write-Host $cMakeArguments
    $cMakeProcess = Start-Process $cMakeProgram -ArgumentList $cMakeArguments -NoNewWindow -Wait -PassThru
    if ($cMakeProcess.ExitCode -ne 0 ) {
        throw "cmake failed generation for '$cMakeArguments' with exit code '$cMakeProcess.ExitCode'"
    }


    if ($runBuild) {
    # CMake Build Process
    
        Write-Host "Building $systemName $architecture $configuration"

        $buildArguments = "--build --preset $configName"

        $buildProcess = Start-Process $cMakeProgram -ArgumentList $buildArguments -NoNewWindow -PassThru

    # Grab the process handle. When using `-NoNewWindow`, retrieving the ExitCode can return null once the process
    # has exited. See: https://stackoverflow.com/questions/44057728/start-process-system-diagnostics-process-exitcode-is-null-with-nonewwindow

        $processHandle = $buildProcess.Handle
        $buildProcess.WaitForExit();
        
        if ($buildProcess.ExitCode -ne 0) {
            throw "cmake failed build for '$buildArguments' with exit code '$buildProcess.ExitCode'"
        }

        $install_directory =""

        if($IsWindows){
            $install_directory = "Result.$systemName.$architecture.MultiConfig"
        }

        else{
            $install_directory = "Result.$systemName.$architecture.$configuration"
        }

        $installProcess = Start-Process $cMakeProgram -ArgumentList "--install $install_directory --prefix $install_directory" -NoNewWindow -PassThru

        $installProcess.WaitForExit();

        if($installProcess.ExitCode -ne 0){
            throw "cmake failed to install to '$install_directory'"
        }
    }
}

if(-Not $LauncherOnly) {

    # Run Clang format
    if ($RunClangFormat) {
        [string]$clangFormatScript = Join-Path $PSScriptRoot -ChildPath "ClangFormat.ps1"
        [string[]]$srcDirectories = @(
            (Join-Path $repositoryRootPath -ChildPath "ZEngine"),
            (Join-Path $repositoryRootPath -ChildPath "Tetragrama")
            (Join-Path $repositoryRootPath -ChildPath "Resources/Shaders")
        )
    
        foreach ($directory in $srcDirectories) {
            & pwsh -File $clangFormatScript -SourceDirectory $directory -RunAsCheck:$VerifyFormatting
    
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Stopped build process..." -ErrorAction Stop
            }
        }
    }

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Stopped build process..." -ErrorAction Stop
    }
}

# Run Engine Build
foreach ($config in $Configurations) {
    Build $config $VsVersion $RunBuilds
}
