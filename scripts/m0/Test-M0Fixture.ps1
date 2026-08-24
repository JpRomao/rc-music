[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [System.PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Import-Module (Join-Path $repositoryRoot 'experiments\m0\harness\M0.Harness.psd1') -Force -ErrorAction Stop
Import-Module (Join-Path $repositoryRoot 'experiments\m0\harness\M0.Fixture.psm1') -Force -ErrorAction Stop

$fixtureRoot = Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'experiments\m0\shared\shell-fixture'
$schemaRoot = Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'experiments\m0\protocol\schemas'
$result = Assert-M0FixtureSet -FixtureRoot $fixtureRoot -SchemaRoot $schemaRoot
Write-Host "OK: $($result.fixture_id), $($result.item_count) queue items, $($result.manifest_file_count) licensed files, fixture SHA-256 $($result.fixture_sha256)."
