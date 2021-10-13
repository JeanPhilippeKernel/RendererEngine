#Requires -PSEdition Core

param (
    [ValidateSet('Windows', 'Linux')]
    [string[]] $SystemNames = @('Windows', 'Linux'),

    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = @('Debug', 'Release')
)

$ErrorActionPreference = "Stop"
[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")
[string]$OuputBuildDirectory = [IO.Path]::Combine($RepoRoot, "out\build")
[string]$ExamplesDirectory = [IO.Path]::Combine($RepoRoot, "Examples")


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
            @{ From = "$ExamplesDirectory\Images";  To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\"}
            @{ From = "$ExamplesDirectory\Images";  To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\"}
        )
    }
    @{
        Name = "DLL files"
        IsDirectory = $false
        Contents = @(
            if($SystemNames -eq 'Windows') {
                @{ From = "$OuputBuildDirectory\__externals\SDL2\$Configurations\SDL2d.dll";    To = "$OuputBuildDirectory\Examples\Sandbox\src\$Configurations\SDL2d.dll"}
                @{ From ="$OuputBuildDirectory\__externals\SDL2\$Configurations\SDL2d.dll";     To = "$OuputBuildDirectory\Examples\Sandbox3D\src\$Configurations\SDL2d.dll"}
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