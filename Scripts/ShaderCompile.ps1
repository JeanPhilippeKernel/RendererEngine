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
    [string] $Configuration = 'Debug',

    [Parameter(HelpMessage = "Whether to force shader build, default to False")]
    [bool] $ForceRebuild = $false
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot Shared.ps1)

$glslValidatorProgram = Find-GlslangValidator

$shaderDirectory = Join-Path $repositoryRootPath -ChildPath "Resources\Shaders"
$shaderCacheDirectory = Join-Path $repositoryRootPath -ChildPath "Resources\Shaders\Cache"

# Ensure the Shader Cache directory exists, or create it otherwise
if(-not (Test-Path $shaderCacheDirectory)) {
    $Null = New-Item -ItemType Directory -Path $shaderCacheDirectory -ErrorAction SilentlyContinue
}

$shaderSourceFiles = Get-ChildItem $shaderDirectory -Recurse -File | Where-Object {$_.Name -like "*.vert" -or $_.Name -like "*.frag"}
$cacheFilesCount = (Get-ChildItem $shaderCacheDirectory -Recurse | Measure-Object).Count;

if (($cacheFilesCount -gt 0) -and (-not $ForceRebuild)) {
    Write-Host "Shader files have already been compiled, skipping..."
    return;
}

if ($shaderSourceFiles.Count -eq 0) {
    Write-Warning "No Shader files found..."
    return;
}

# Create include directories
$shaderIncludeDirectories = @();
foreach ($shaderFile in $shaderSourceFiles) {
    if(!$shaderIncludeDirectories.Contains($shaderFile.DirectoryName)) {
        $shaderIncludeDirectories += $shaderFile.DirectoryName;
    }
}

[string] $includeDirArgs = "";
foreach ($includeDir in $shaderIncludeDirectories) {
    $includeDirArgs += " -I$includeDir"
}

[string] $compilerArgs = "";
if ($Configuration -eq "Debug") {
    $compilerArgs = " -g"
}
else {
    $compilerArgs = " -Os"
}

foreach ($shaderFile in $shaderSourceFiles) {
    $fileName = $shaderFile.BaseName
    $suffixName = If($shaderFile.Extension -eq ".vert") { "_vertex" } Else { "_fragment" }
    $outputFileFullName = Join-Path $shaderCacheDirectory -ChildPath "$fileName$suffixName.spv"

    $fileFullName = $shaderFile.FullName;
    Write-Host "Compiling Shader file : $fileFullName"
    $glslValidatorProcess = Start-Process $glslValidatorProgram -ArgumentList "$compilerArgs -V $fileFullName -o $outputFileFullName $includeDirArgs" -NoNewWindow -PassThru
    $handle = $glslValidatorProcess.Handle;
    $glslValidatorProcess.WaitForExit();
    $exitCode = $glslValidatorProcess.ExitCode

    if ($exitCode -ne 0) {
        Write-Error "failed to compile $shaderFile ..." -ErrorAction Stop
    }
}

Write-Host "Shader files compilation successful..."
