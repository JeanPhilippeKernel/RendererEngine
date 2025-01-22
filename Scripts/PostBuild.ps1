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
    [Parameter(HelpMessage="SystemName type to build, default to Windows")]
    [ValidateSet('Windows', 'Darwin', 'Linux')]
    [string[]] $SystemName = 'Windows',

    [Parameter(HelpMessage="Architecture type to build, default to x64")]
    [ValidateSet('win-x64', 'arm64', 'osx-x64', 'osx-arm64')]
    [string[]] $Architectures = 'win-x64',

    [Parameter(HelpMessage="Configuration type to build, default to Debug")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = 'Debug',

    [Parameter(HelpMessage = "Build Launcher only")]
    [switch] $LauncherOnly
)

$ErrorActionPreference = "Stop"
$TargetFramework = 'net8.0'

[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")
[string]$OuputBuildDirectory = If($IsWindows) {
    [IO.Path]::Combine($RepoRoot, "Result.Windows.x64.MultiConfig")
}  Else {
    [IO.Path]::Combine($RepoRoot, "Result.$SystemName.x64.$Configurations")
}

if($LauncherOnly) {
    Write-Host "Skipping resources copy..."
    return;
}

$ContentsToProcess = @(
    @{
        Name = "Resources"
        IsDirectory = $true
        Contents = @(
            switch ($SystemName) {
                "Windows" {
                    @{ From = "$RepoRoot\Resources\Shaders";            To = "$OuputBuildDirectory\Tetragrama\$Configurations\Shaders"}
                    @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\$Configurations\Settings"}
                }
                "Darwin" {
                    @{ From = "$RepoRoot\Resources\Shaders";            To = "$OuputBuildDirectory\Tetragrama\$Configurations\Shaders"}
                    @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\$Configurations\Settings"}
                }
                "Linux" {
                    @{ From = "$RepoRoot\Resources\Shaders";            To = "$OuputBuildDirectory\Tetragrama\Shaders"}
                    @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\Settings"}
                }
                Default {
                    throw 'This system is not supported'
                }
            }
        )
    },
    @{
        Name = "Tetragrama"
        IsDirectory = $true
        Contents = @(
            switch ($SystemName) {
                "Windows" {
                    @{ From = "$OuputBuildDirectory\Tetragrama\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\$TargetFramework\Editor"}
                    @{ From = "$OuputBuildDirectory\Tetragrama\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\$TargetFramework\$Architectures\publish\Editor"}
                }
                "Darwin" {
                    switch ($Architectures) {
                        "osx-x64" {
                            @{ From = "$OuputBuildDirectory\Tetragrama\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\$TargetFramework\$Architectures\Editor"}
                            @{ From = "$OuputBuildDirectory\Tetragrama\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\$TargetFramework\$Architectures\publish\Editor"}
                        }
                        "osx-arm64" {
                            @{ From = "$OuputBuildDirectory\Tetragrama\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\$TargetFramework\$Architectures\Editor"}
                            @{ From = "$OuputBuildDirectory\Tetragrama\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\$TargetFramework\$Architectures\publish\Editor"}
                        }
                        Default {
                            throw 'This architecture is not supported'
                        }
                    }
                }
                "Linux" {
                    @{ From = "$OuputBuildDirectory\Tetragrama\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\$TargetFramework\Editor"}
                }
                Default {
                    throw 'This system is not supported'
                }
            }
        )
    }
)

foreach ($item in $ContentsToProcess) {
    foreach($content in $item.Contents) {

        if($item.IsDirectory -eq $true) {

            # Delete if directories or files already exist
            #
            if(Test-Path $content.To) {
                Remove-Item $content.To -Recurse -Force
            }
            Copy-Item -Path $content.From -Destination $content.To -Recurse -Force
        }
        else {
            Copy-Item -Path $content.From -Destination $content.To -Force
        }

        [string]$name = $item.Name
        [string]$ToDirectory = $content.To
        Write-Host "Copied $name --> $ToDirectory"
    }
}