[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$entrypointModuleNames = @('M0.Harness', 'M0.Summary', 'M0.Fixture', 'M0.Markers', 'M0.Statistics', 'M0.Visual', 'M0.Process')
$preloadedEntrypointModules = @(Get-Module -Name $entrypointModuleNames)
if ($preloadedEntrypointModules.Count -ne 0) {
    $loadedNames = @($preloadedEntrypointModules | ForEach-Object Name | Sort-Object -Unique) -join ', '
    throw [IO.InvalidDataException]::new("M0Q040 Test-M0Summary requires a clean PowerShell process; preloaded module(s) '$loadedNames' were preserved and no module was removed or reloaded.")
}
foreach ($nativeTypeName in @(
    'M0ProcessNativeV1.PilotSession',
    'M0ProcessNativeV1.ProcessCapability',
    'M0ProcessNativeV1.TrustedFile'
)) {
    if ($nativeTypeName -as [type]) {
        throw [IO.InvalidDataException]::new("M0Q040 Test-M0Summary requires a clean PowerShell process; preloaded native type '$nativeTypeName' is forbidden before trusted module import.")
    }
}

function Import-M0SummaryExactEntrypointModule {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $ImportPath,
        [Parameter(Mandatory)] [string] $ExpectedLoadedPath
    )

    $expected = [IO.Path]::GetFullPath($ExpectedLoadedPath)
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (@(Get-Module -Name $Name).Count -ne 0) {
        throw [IO.InvalidDataException]::new("M0Q040 module '$Name' appeared after the clean-process gate; no module was removed or reloaded.")
    }
    Import-Module -Name ([IO.Path]::GetFullPath($ImportPath)) -ErrorAction Stop
    $loaded = @(Get-Module -Name $Name)
    if ($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($expected, $comparison)) {
        throw [IO.InvalidDataException]::new("M0Q040 module '$Name' did not bind to the exact repository path '$expected'.")
    }
    return $loaded[0]
}

$harnessRoot = Join-Path $repositoryRoot 'experiments\m0\harness'
$null = Import-M0SummaryExactEntrypointModule -Name 'M0.Harness' `
    -ImportPath (Join-Path $harnessRoot 'M0.Harness.psd1') -ExpectedLoadedPath (Join-Path $harnessRoot 'M0.Harness.psm1')

$protocolRoot = Join-Path $repositoryRoot 'experiments\m0\protocol'
$schemaRoot = Join-Path $protocolRoot 'schemas'
$protocol = M0.Harness\Assert-M0JsonDocument `
    -DocumentPath (Join-Path $protocolRoot 'protocol.v1.json') -SchemaPath (Join-Path $schemaRoot 'protocol.schema.json')
$null = M0.Harness\Assert-M0HarnessManifest `
    -RepositoryRoot $repositoryRoot -ExpectedManifestSha256 $protocol.harness_manifest_sha256
$commonInputs = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $repositoryRoot -LoadSummary

$expectedSummaryPath = [IO.Path]::GetFullPath((Join-Path $harnessRoot 'M0.Summary.psm1'))
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$trustedSummary = @(Get-Module -All -Name 'M0.Summary')
if ($trustedSummary.Count -ne 1 -or
    -not [IO.Path]::GetFullPath($trustedSummary[0].Path).Equals($expectedSummaryPath, $comparison)) {
    throw [IO.InvalidDataException]::new("M0Q040 M0.Summary was not retained from the manifest-locked exact repository path '$expectedSummaryPath'.")
}
$summaryModule = $trustedSummary[0]
$contract = & $summaryModule {
    param($root, $expectedProtocolSha256)
    Assert-M0SummaryArtifacts -ProtocolRoot $root -ExpectedProtocolSha256 $expectedProtocolSha256
} $protocolRoot $commonInputs.protocol_sha256

Write-Host "OK: $($contract.summary_id); deterministic common summary SHA-256 $($contract.golden_sha256)."
