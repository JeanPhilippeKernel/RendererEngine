# Specify the directory where you want to install NuGet
$installPath = "$env:USERPROFILE\NuGet"
$nugetPath = "$installPath\nuget.exe"

# Exit the script if nuget.exe already exists, this assumes that nuget has been added to path
if (Test-Path $nugetPath) {
    Write-Host "Nuget already installed"
    exit
}

# Create the directory if it doesn't exist
if (-not (Test-Path $installPath)) {
    New-Item -ItemType Directory -Path $installPath
}

# Download nuget.exe since it does not exist
Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nugetPath

# Function to add a path to the system PATH environment variable 
$scriptBlock = {
    function Add-ToSystemPath {
        $installPath = "$env:USERPROFILE\NuGet"
        $path = [Environment]::GetEnvironmentVariable("Path", [EnvironmentVariableTarget]::Machine)

        if ($path -notlike "*$installPath*") {
            $newPath = $path + ";" + $installPath
            [Environment]::SetEnvironmentVariable("Path", $newPath, [EnvironmentVariableTarget]::Machine)
            Write-Host "Path added to system PATH: $installPath"
        }
        else {
            Write-Host "Path already exists in system PATH: $installPath"
        }
    }

    Add-ToSystemPath
}

# Add NuGet to system PATH
$encodedScript = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($scriptBlock.ToString()))

# Use Start-Process to run the script block with admin rights
Start-Process pwsh -ArgumentList "-NoProfile -EncodedCommand $encodedScript" -Verb RunAs -Wait
Write-Host "Installation and configuration completed successfully!"

# Update the current session's PATH environment variable
$installPath = "$env:USERPROFILE\NuGet"
$env:Path += ";$installPath"