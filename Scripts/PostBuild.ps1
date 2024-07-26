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
    [Parameter(HelpMessage="Configuration type to build, default to Debug")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = 'Debug',

    [Parameter(HelpMessage = "Build Launcher only")]
    [switch] $LauncherOnly
)

$ErrorActionPreference = "Stop"

# Check the system name
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

[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")
[string]$OuputBuildDirectory = If($IsWindows) { 
    [IO.Path]::Combine($RepoRoot, "Result.Windows.x64.MultiConfig") 
}  Else { 
    [IO.Path]::Combine($RepoRoot, "Result.$SystemName.x64.$Configurations")
}

if(-not $LauncherOnly)
{ 
    $ContentsToProcess = @(
        @{
            Name = "Resources"
            IsDirectory = $true
            Contents = @(
            
                if ($IsWindows) {
                    @{ From = "$RepoRoot\Resources\Shaders";            To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Shaders"}
                    @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Settings"}
                }
                elseif ($IsMacOS) {
                    @{ From = "$RepoRoot\Resources\Shaders";            To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Shaders"}
                    @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Settings"}
                }
                else {
                    @{ From = "$RepoRoot\Resources\Shaders";            To = "$OuputBuildDirectory\Tetragrama\src\Shaders"}
                    @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\src\Settings"}
                }
            )
        },
        @{
            Name = "Tetragrama"
            IsDirectory = $true
            Contents = @(
                @{ From = "$OuputBuildDirectory\Tetragrama\src\$Configurations"; To = "$OuputBuildDirectory\Panzerfaust\$Configurations\net6.0\Editor"}
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
}
