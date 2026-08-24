[CmdletBinding()]
param(
    [string] $RawPath,
    [string] $RunPath,
    [string] $BatchPath,
    [string[]] $BatchRunPath = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [System.PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Import-Module (Join-Path $repositoryRoot 'experiments\m0\harness\M0.Harness.psd1') -Force -ErrorAction Stop
Import-Module (Join-Path $repositoryRoot 'experiments\m0\harness\M0.Evidence.psm1') -Force -ErrorAction Stop

if ($BatchRunPath.Count -gt 0 -and -not $BatchPath) {
    throw [ArgumentException]::new('BatchRunPath is unavailable without public canonical batch validation.')
}
if (-not $RawPath -and -not $RunPath -and -not $BatchPath) {
    & (Join-Path $repositoryRoot 'scripts\m0\Invoke-M0HarnessTests.ps1') -TestFile 'Evidence.Tests.ps1'
    return
}
if ([string]::IsNullOrWhiteSpace($RawPath) -xor [string]::IsNullOrWhiteSpace($RunPath)) {
    throw [ArgumentException]::new('RawPath and RunPath must be provided together.')
}
if ($BatchPath) {
    $null = Get-M0TrustedCommonInputs -RepositoryRoot $repositoryRoot
    throw [IO.InvalidDataException]::new('M0E017 public decisive batch validation remains fail-closed after trusted harness/common-input validation until external candidate artifact, toolchain and lockfile provenance exists.')
}
$outputRoot = Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'out\m0'
$schemaRoot = Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'experiments\m0\protocol\schemas'
$runResult = $null
if ($RawPath) {
    $resolvedRaw = Resolve-M0ScopedPath -BasePath $outputRoot -CandidatePath $RawPath
    $resolvedRun = Resolve-M0ScopedPath -BasePath $outputRoot -CandidatePath $RunPath
    $runResult = Assert-M0EvidenceRun -RawPath $resolvedRaw -RunPath $resolvedRun -SchemaRoot $schemaRoot
}

[pscustomobject]@{
    run = $runResult
    batch = $null
}
