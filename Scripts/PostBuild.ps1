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
            if ($SystemNames -eq "Windows"){
                @{ From = "$RepoRoot\Resources";    To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\"}
                @{ From = "$RepoRoot\Resources";    To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\"}
            }
            else {
                @{ From = "$RepoRoot\Resources";    To = "$OuputBuildDirectory\Examples\Sandbox\src\"}
                @{ From = "$RepoRoot\Resources";    To = "$OuputBuildDirectory\Examples\Sandbox3D\src\"}
            }
        )
    }
    @{
        Name = "Assets"
        IsDirectory = $true
        Contents = @(
            if ($SystemNames -eq "Windows") {
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\"}
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\"}
            }
            else {
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox\src\"}
                @{ From = "$RepoRoot\Assets";   To = "$OuputBuildDirectory\Examples\Sandbox3D\src\"}
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