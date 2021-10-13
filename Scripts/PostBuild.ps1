#Requires -PSEdition Core

param (
    [Parameter(HelpMessage="System name to build, default to all")]
    [ValidateSet('Windows', 'Linux')]
    [string[]] $SystemNames = @('Windows', 'Linux'),

    [Parameter(HelpMessage="Configuration type to build, default to Debug")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = 'Debug'
)

$ErrorActionPreference = "Stop"
[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")
[string]$OuputBuildDirectory = [IO.Path]::Combine($RepoRoot, "out\build")

$ContentsToProcess = @(
    @{
        Name = "Resources"
        IsDirectory = $true
        Contents = @(
            @{ From = "$RepoRoot\Resources";        To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\"}
            @{ From = "$RepoRoot\Resources";        To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\"}
        )
    }
    @{
        Name = "Assets"
        IsDirectory = $true
        Contents = @(
            @{ From = "$RepoRoot\Assets";  To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\"}
            @{ From = "$RepoRoot\Assets";  To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\"}
        )
    }
    @{
        Name = "DLL files"
        IsDirectory = $false
        Contents = @(
            if($SystemNames -eq 'Windows') {
                if($Configurations -eq 'Debug'){
                    @{ From = "$OuputBuildDirectory\__externals\SDL2\$Configurations\SDL2d.dll";    To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\SDL2d.dll"}
                }
                else {
                    @{ From ="$OuputBuildDirectory\__externals\SDL2\$Configurations\SDL2.dll";     To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\SDL2.dll"}
                }
            }
            else {
                #Todo: provide dir on linux system
            }
        )
    }
)

foreach ($item in $ContentsToProcess) {
    foreach($content in $item.Contents) {
        if($item.IsDirectory -eq $true) {
            Copy-Item -Path $content.From -Destination $content.To -Recurse -Force
        }
        else {
            Copy-Item -Path $content.From -Destination $content.To -Force
        }
    }
    $name = $item.Name
    Write-Host "Copied $name"
}