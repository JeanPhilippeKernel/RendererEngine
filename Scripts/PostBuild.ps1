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
    [string[]] $Configurations = 'Debug'
)

$ErrorActionPreference = "Stop"

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

[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")
[string]$OuputBuildDirectory = If($SystemName -eq 'Windows') { 
    [IO.Path]::Combine($RepoRoot, "Result.Windows.x64.MultiConfig") 
}  Else { 
    [IO.Path]::Combine($RepoRoot, "Result.$SystemName.x64.$Configurations")
}

$ContentsToProcess = @(
    @{
        Name = "Resources"
        IsDirectory = $true
        Contents = @(
            if ($SystemName -eq "Windows"){
                @{ From = "$RepoRoot\Resources\Windows";            To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Resources\Windows"}
                @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Settings"}
                @{ From = "$RepoRoot\Resources\Editor\Fonts";       To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Settings\Fonts"}
            }
            elseif ($SystemName -eq "Darwin") {
                @{ From = "$RepoRoot\Resources\Unix";               To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Resources\Unix"}
                @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Settings"}
                @{ From = "$RepoRoot\Resources\Editor\Fonts";       To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Settings\Fonts"}
            }
            else {
                @{ From = "$RepoRoot\Resources\Unix";               To = "$OuputBuildDirectory\Tetragrama\src\Resources\Unix"}
                @{ From = "$RepoRoot\Resources\Editor\Settings";    To = "$OuputBuildDirectory\Tetragrama\src\Settings"}
                @{ From = "$RepoRoot\Resources\Editor\Fonts";       To = "$OuputBuildDirectory\Tetragrama\src\Settings\Fonts"}
            }
        )
    }
    @{
        Name = "Assets"
        IsDirectory = $true
        Contents = @(
            if ($SystemName -eq "Windows") {
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Assets"}
            }
            elseif ($SystemName -eq "Darwin") {
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Tetragrama\src\$Configurations\Assets"}
            }
            else {
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Tetragrama\src\Assets"}
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