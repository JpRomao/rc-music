[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$modulePath = Join-Path $repositoryRoot 'experiments\m0\harness\M0.Markers.psm1'
$protocolRoot = Join-Path $repositoryRoot 'experiments\m0\protocol'
Import-Module $modulePath -Force -ErrorAction Stop

$result = Assert-M0MarkerArtifacts -ProtocolRoot $protocolRoot
Write-Host "OK: $($result.catalog_id), $($result.scenario_count) shell scenarios, marker catalog SHA-256 $($result.catalog_sha256)."
