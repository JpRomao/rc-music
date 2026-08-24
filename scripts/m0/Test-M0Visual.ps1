[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$entrypointModuleNames = @('M0.Harness', 'M0.Visual')
$preloadedEntrypointModules = @(Get-Module -Name $entrypointModuleNames)
if ($preloadedEntrypointModules.Count -ne 0) {
    $loadedNames = @($preloadedEntrypointModules | ForEach-Object Name | Sort-Object -Unique) -join ', '
    throw [IO.InvalidDataException]::new("M0V040 Test-M0Visual requires a clean PowerShell process; preloaded module(s) '$loadedNames' were preserved and no module was removed or reloaded.")
}

function Import-M0VisualExactEntrypointModule {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $ImportPath,
        [Parameter(Mandatory)] [string] $ExpectedLoadedPath
    )

    $expected = [IO.Path]::GetFullPath($ExpectedLoadedPath)
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (@(Get-Module -Name $Name).Count -ne 0) {
        throw [IO.InvalidDataException]::new("M0V040 module '$Name' appeared after the clean-process gate; no module was removed or reloaded.")
    }
    Import-Module -Name ([IO.Path]::GetFullPath($ImportPath)) -ErrorAction Stop
    $loaded = @(Get-Module -Name $Name)
    if ($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($expected, $comparison)) {
        throw [IO.InvalidDataException]::new("M0V040 module '$Name' did not bind to the exact repository path '$expected'.")
    }
    return $loaded[0]
}

$harnessRoot = Join-Path $repositoryRoot 'experiments\m0\harness'
$null = Import-M0VisualExactEntrypointModule -Name 'M0.Harness' `
    -ImportPath (Join-Path $harnessRoot 'M0.Harness.psd1') -ExpectedLoadedPath (Join-Path $harnessRoot 'M0.Harness.psm1')

$protocolRoot = Join-Path $repositoryRoot 'experiments\m0\protocol'
$schemaRoot = Join-Path $protocolRoot 'schemas'
$protocol = M0.Harness\Assert-M0JsonDocument `
    -DocumentPath (Join-Path $protocolRoot 'protocol.v1.json') -SchemaPath (Join-Path $schemaRoot 'protocol.schema.json')
$null = M0.Harness\Assert-M0HarnessManifest `
    -RepositoryRoot $repositoryRoot -ExpectedManifestSha256 $protocol.harness_manifest_sha256
$null = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $repositoryRoot

$expectedVisualPath = [IO.Path]::GetFullPath((Join-Path $harnessRoot 'M0.Visual.psm1'))
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$trustedVisual = @(Get-Module -All -Name 'M0.Visual')
if ($trustedVisual.Count -ne 1 -or
    -not [IO.Path]::GetFullPath($trustedVisual[0].Path).Equals($expectedVisualPath, $comparison)) {
    throw [IO.InvalidDataException]::new("M0V040 M0.Visual was not loaded from the exact trusted repository path '$expectedVisualPath'.")
}
$null = Import-M0VisualExactEntrypointModule -Name 'M0.Visual' `
    -ImportPath $expectedVisualPath -ExpectedLoadedPath $expectedVisualPath
$result = M0.Visual\Assert-M0VisualArtifacts -ProtocolRoot $protocolRoot

Write-Host "OK: $($result.visual_id), synthetic detector golden $($result.golden_id), descriptor SHA-256 $($result.descriptor_sha256)."
