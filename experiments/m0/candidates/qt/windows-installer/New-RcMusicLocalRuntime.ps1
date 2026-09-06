#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [string] $BuildDirectory
)

# Compatibility entrypoint only; the runtime has one implementation in the product.
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repository 'out/m0/builds/qt-audio/m43-production-release-20260901'
}
& (Join-Path $repository 'packaging/windows/New-RcMusicLocalRuntime.ps1') `
    -OutputDirectory $OutputDirectory -BuildDirectory $BuildDirectory
