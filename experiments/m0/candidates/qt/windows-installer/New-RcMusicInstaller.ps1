#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $RuntimeDirectory,
    [Parameter(Mandatory)][string] $OutputDirectory,
    [string] $MakeNsis,
    [string] $FormatMatrix
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
if (-not $MakeNsis) { $MakeNsis = Join-Path $repository 'out/m0/toolchains/nsis-3.12/makensis.exe' }
& (Join-Path $repository 'packaging/windows/New-RcMusicInstaller.ps1') `
    -RuntimeDirectory $RuntimeDirectory -OutputDirectory $OutputDirectory -MakeNsis $MakeNsis `
    -FormatMatrix $FormatMatrix -LegacyBaseline
