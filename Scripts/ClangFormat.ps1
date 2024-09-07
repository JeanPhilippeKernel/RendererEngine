# MIT License

# Copyright (c) 2024 Jean Philippe & Contributors

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
    [Parameter(Mandatory, HelpMessage = "The directory that contains source code to format")]
    [string]$SourceDirectory,

    [Parameter(HelpMessage = "Whether clang-format should only check if the source code is well-formatted")]
    [bool]$RunAsCheck=$False
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot Shared.ps1)

$srcFiles = Get-ChildItem -Path $SourceDirectory -Recurse -File | Where-Object { $_.Name -notlike "CMakeLists*" -and $_.Name -notlike "*.json" }

if ($srcFiles.Count -eq 0) {
    Write-Host "No source files found in the specified directory."
    return
}

$srcFiles = $srcFiles | ForEach-Object { $_.FullName }

Write-Host "Running clang-format on $SourceDirectory..."

[string[]] $clangFormatArgument = "-i", "--ferror-limit=0", "-fallback-style=none", "--style=file"

if ($RunAsCheck) {
    $clangFormatArgument += "--dry-run", "--Werror"
}

$clangFormatProgram = Find-ClangFormat

$process = Start-Process $clangFormatProgram -ArgumentList "$clangFormatArgument $srcFiles" -NoNewWindow -PassThru
$handle = $process.Handle
$process.WaitForExit()
$exitCode = $process.ExitCode

if ($exitCode -ne 0) {
    Write-Error "clang-format failed formatting source with exit code '$exitCode'" -ErrorAction Stop
}
else {
    Write-Host "clang-format source formatting succeeded"
}