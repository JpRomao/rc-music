[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$modulePath = Join-Path $repositoryRoot 'experiments\m0\audio-corpus\M0.AudioCorpus.psm1'
$schemaRoot = Join-Path $repositoryRoot 'experiments\m0\audio-corpus\schemas'
$corpusRoot = Join-Path $repositoryRoot 'experiments\m0\shared\audio-corpus'

Import-Module $modulePath -Force -ErrorAction Stop
$result = Assert-M0AudioCorpus -CorpusRoot $corpusRoot -SchemaRoot $schemaRoot
Write-Host (
    'OK: {0}, {1} fixtures ({2} valid, {3} hostile), catalog {4}, manifest SHA-256 {5}.' -f
    $result.corpus_id,
    $result.fixture_count,
    $result.valid_fixture_count,
    $result.hostile_fixture_count,
    $result.catalog_status,
    $result.manifest_sha256
)
