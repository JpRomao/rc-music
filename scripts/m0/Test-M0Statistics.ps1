[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$modulePath = Join-Path $repositoryRoot 'experiments\m0\harness\M0.Statistics.psm1'
$protocolRoot = Join-Path $repositoryRoot 'experiments\m0\protocol'
Import-Module $modulePath -Force -ErrorAction Stop

$result = Assert-M0StatisticsArtifacts -ProtocolRoot $protocolRoot
Write-Host (
    "OK: $($result.protocol_id), $($result.block_count) paired blocks " +
    "($($result.ab_blocks) AB/$($result.ba_blocks) BA), schedule SHA-256 $($result.schedule_sha256)."
)
