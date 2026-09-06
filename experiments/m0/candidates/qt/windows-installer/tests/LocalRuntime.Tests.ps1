#Requires -Version 7.0
[CmdletBinding()]
param([string] $OutputDirectory, [string] $BuildDirectory)

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repository 'out/m0/builds/qt-audio/m43-production-release-20260901'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repository ('out/m0/rc-music-local/' + [Guid]::NewGuid().ToString('N'))
}
& (Join-Path $repository 'packaging/windows/tests/LocalRuntime.Tests.ps1') `
    -OutputDirectory $OutputDirectory -BuildDirectory $BuildDirectory
