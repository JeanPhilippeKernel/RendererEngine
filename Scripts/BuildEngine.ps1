#Requires -PSEdition Core

param (
    [Parameter(HelpMessage="System name to build, default to all")]
    [ValidateSet('Windows', 'Linux')]
    [string[]] $SystemNames = @('Windows', 'Linux'),

    [Parameter(HelpMessage="Architecture type to build, default to x64")]
    [string] $Architectures = 'x64',

    [Parameter(HelpMessage="Configuration type to build, default to Debug")]
    [ValidateSet('Debug', 'Release')]
    [string[]] $Configurations = 'Debug',

    [Parameter(HelpMessage="Whether to run build, default to True")]
    [bool] $RunBuild = $True
)

$ErrorActionPreference = "Stop"