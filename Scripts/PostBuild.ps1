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

    [Parameter(HelpMessage="Configuration type to build, default to Debug")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = 'Debug'
)

$ErrorActionPreference = "Stop"
[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")
[string]$OuputBuildDirectory = If($SystemNames -eq 'Windows') { 
    [IO.Path]::Combine($RepoRoot, "Result.Windows.x64.MultiConfig") 
}  Else { 
    [IO.Path]::Combine($RepoRoot, "Result.$SystemNames.x64.$Configurations")
}

$ContentsToProcess = @(
    @{
        Name = "Resources"
        IsDirectory = $true
        Contents = @(
            if ($SystemNames -eq "Windows"){
                @{ From = "$RepoRoot\Resources\Windows";    To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\Resources\Windows"}
                @{ From = "$RepoRoot\Resources\Windows";    To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\Resources\Windows"}
            }
            else {
                @{ From = "$RepoRoot\Resources\Linux";    To = "$OuputBuildDirectory\Examples\Sandbox\src\Resources\Linux"}
                @{ From = "$RepoRoot\Resources\Linux";    To = "$OuputBuildDirectory\Examples\Sandbox3D\src\Resources\Linux"}
            }
        )
    }
    @{
        Name = "Assets"
        IsDirectory = $true
        Contents = @(
            if ($SystemNames -eq "Windows") {
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\Assets"}
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\Assets"}
            }
            else {
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox\src\Assets"}
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox3D\src\Assets"}
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