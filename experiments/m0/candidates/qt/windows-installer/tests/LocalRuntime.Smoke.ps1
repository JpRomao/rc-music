#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $RuntimeDirectory,
    [string] $InstrumentedBuildDirectory
)

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
if (-not $InstrumentedBuildDirectory) {
    $InstrumentedBuildDirectory = Join-Path $repository 'out/m0/builds/qt-audio/combined-shell-20260829-01'
}
& (Join-Path $repository 'packaging/windows/tests/LocalRuntime.Smoke.ps1') `
    -RuntimeDirectory $RuntimeDirectory -InstrumentedBuildDirectory $InstrumentedBuildDirectory
