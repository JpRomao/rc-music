[CmdletBinding()]
param(
    [string] $RawPath,
    [string] $RunPath,
    [string] $BatchPath,
    [string] $SnapshotSetPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [System.PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$entrypointModuleNames = @('M0.Harness', 'M0.Evidence')
$preloadedEntrypointModules = @(Get-Module -Name $entrypointModuleNames)
if ($preloadedEntrypointModules.Count -ne 0) {
    $loadedNames = @($preloadedEntrypointModules | ForEach-Object { $_.Name } | Sort-Object -Unique) -join ', '
    throw [IO.InvalidDataException]::new("M0E027 Test-M0Evidence requires a clean PowerShell process; preloaded module(s) '$loadedNames' were preserved and no module was removed or reloaded.")
}

function Import-M0ExactEntrypointModule {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $ImportPath,
        [Parameter(Mandatory)] [string] $ExpectedLoadedPath
    )

    $expected = [IO.Path]::GetFullPath($ExpectedLoadedPath)
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    $loaded = @(Get-Module -Name $Name)
    if ($loaded.Count -ne 0) {
        throw [IO.InvalidDataException]::new("M0E027 module '$Name' appeared after the clean-process gate; no module was removed or reloaded.")
    }
    Import-Module -Name ([IO.Path]::GetFullPath($ImportPath)) -ErrorAction Stop
    $loaded = @(Get-Module -Name $Name)
    if ($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($expected, $comparison)) {
        throw [IO.InvalidDataException]::new("M0E027 module '$Name' did not bind to the exact repository path '$expected'.")
    }
    return $loaded[0]
}

$harnessRoot = Join-Path $repositoryRoot 'experiments\m0\harness'
$null = Import-M0ExactEntrypointModule -Name 'M0.Harness' `
    -ImportPath (Join-Path $harnessRoot 'M0.Harness.psd1') -ExpectedLoadedPath (Join-Path $harnessRoot 'M0.Harness.psm1')
$null = Import-M0ExactEntrypointModule -Name 'M0.Evidence' `
    -ImportPath (Join-Path $harnessRoot 'M0.Evidence.psm1') -ExpectedLoadedPath (Join-Path $harnessRoot 'M0.Evidence.psm1')

$schemaRoot = Join-Path $repositoryRoot 'experiments\m0\protocol\schemas'
$protocolPath = Join-Path $repositoryRoot 'experiments\m0\protocol\protocol.v1.json'
$protocol = M0.Harness\Assert-M0JsonDocument -DocumentPath $protocolPath -SchemaPath (Join-Path $schemaRoot 'protocol.schema.json')
$null = M0.Harness\Assert-M0HarnessManifest -RepositoryRoot $repositoryRoot -ExpectedManifestSha256 $protocol.harness_manifest_sha256

if ($SnapshotSetPath -and -not $BatchPath) {
    throw [ArgumentException]::new('SnapshotSetPath is unavailable without BatchPath.')
}
if (-not $RawPath -and -not $RunPath -and -not $BatchPath) {
    & (Join-Path $repositoryRoot 'scripts\m0\Invoke-M0HarnessTests.ps1') -TestFile 'Evidence.Tests.ps1'
    return
}
if ([string]::IsNullOrWhiteSpace($RawPath) -xor [string]::IsNullOrWhiteSpace($RunPath)) {
    throw [ArgumentException]::new('RawPath and RunPath must be provided together.')
}
if ($BatchPath) {
    if ([string]::IsNullOrWhiteSpace($SnapshotSetPath)) {
        throw [ArgumentException]::new('BatchPath requires the committed SnapshotSetPath; loose run paths are forbidden.')
    }
    $null = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $repositoryRoot
    throw [IO.InvalidDataException]::new('M0E017 public decisive batch validation remains fail-closed after trusted harness/common-input validation until external candidate artifact, toolchain and lockfile provenance exists.')
}
$outputRoot = M0.Harness\Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'out\m0'
$schemaRoot = M0.Harness\Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'experiments\m0\protocol\schemas'
$runResult = $null
if ($RawPath) {
    $resolvedRaw = M0.Harness\Resolve-M0ScopedPath -BasePath $outputRoot -CandidatePath $RawPath
    $resolvedRun = M0.Harness\Resolve-M0ScopedPath -BasePath $outputRoot -CandidatePath $RunPath
    $runResult = M0.Evidence\Assert-M0EvidenceRun -EvidenceRoot (Split-Path $resolvedRun -Parent) -RawPath $resolvedRaw -RunPath $resolvedRun -SchemaRoot $schemaRoot
}

[pscustomobject]@{
    run = $runResult
    batch = $null
}
