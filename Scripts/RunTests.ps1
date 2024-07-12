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
    [Parameter(HelpMessage = "Configuration type to build, default to Debug")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = 'Debug'
)

$ErrorActionPreference = "Stop"

# Check the system name
if ($IsLinux) {
    $SystemName = "Linux"
}
elseif ($IsMacOS) {
    $SystemName = "Darwin"
}
elseif ($IsWindows) {
    $SystemName = "Windows"
}
else {
    throw 'The OS is not supported'
}

[string]$RepoRoot = [IO.Path]::Combine($PSScriptRoot, "..")
[string]$OutputBuildDirectory = If ($IsWindows) {
    [IO.Path]::Combine($RepoRoot, "Result.Windows.x64.MultiConfig")
}
Else {
    [IO.Path]::Combine($RepoRoot, "Result.$SystemName.x64.$Configurations")
}

# Function to run tests
function RunTests {
    param (
        [string]$Configuration
    )
    
    [string]$testExecutablePath = ""
    switch ($SystemName) {
        "Windows" {
            $testExecutablePath = [IO.Path]::Combine($OutputBuildDirectory, "ZEngine", "tests", $Configuration, "ZEngineTests.exe")
        }
        "Linux" {}
        "Darwin" {}
        Default {
            throw 'This system is not supported'
        }
    }
   
    # Check if the executable exists
    if (Test-Path $testExecutablePath) {
        Write-Host "Running tests in $Configuration configuration..."
        
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $testExecutablePath
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.UseShellExecute = $false
        
        try {
            $process = [System.Diagnostics.Process]::Start($psi)
            $output = $process.StandardOutput.ReadToEnd()
            $error_output = $process.StandardError.ReadToEnd()
            $process.WaitForExit()
            
            Write-Host "Standard Output: $output"
            Write-Host "Error Output: $error_output"
            Write-Host "Exit Code: " $process.ExitCode
            
            if ($process.ExitCode -ne 0) {
                Write-Host "Test executable exited with code $($process.ExitCode)"
                
                # Additional error information for DLL not found
                if ($process.ExitCode -eq -1073741515) {
                    Write-Host "This error code typically indicates a missing DLL."
                    Write-Host "Check the Error Output above for any mentions of specific DLLs."
                    Write-Host "You may need to use a tool like Process Monitor in your local environment to identify the missing DLL."
                }
                
                exit $process.ExitCode
            }
        } 
        catch {
            Write-Error "Error running test executable: $_"
            Write-Host "An error occurred:"
            Write-Host $_.Exception.Message
            Write-Host "Stack Trace:"
            Write-Host $_.ScriptStackTrace
            exit 1
        }
    }
    else {
        Write-Warning "Test executable does not exist: $testExecutablePath"
    }
}

# Run tests for each configuration
foreach ($config in $Configurations) {
    RunTests -Configuration $config
}