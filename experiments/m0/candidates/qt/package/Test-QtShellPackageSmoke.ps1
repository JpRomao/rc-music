#requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $PackageRoot,
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $ManifestPath,
    [Parameter(Mandatory)] [ValidatePattern('^[0-9a-fA-F]{64}$')] [string] $ExpectedManifestSha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$smokeModule = Import-Module -Name (Join-Path $PSScriptRoot 'QtShellPackageSmoke.psm1') -Force -PassThru
& $smokeModule.ExportedCommands['Test-QtShellPackageSmoke'] @PSBoundParameters
