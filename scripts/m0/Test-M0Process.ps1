[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$entrypointModuleNames = @('M0.Harness', 'M0.Process')
$preloadedEntrypointModules = @(Get-Module -Name $entrypointModuleNames)
if ($preloadedEntrypointModules.Count -ne 0) {
    $loadedNames = @($preloadedEntrypointModules | ForEach-Object Name | Sort-Object -Unique) -join ', '
    throw [IO.InvalidDataException]::new("M0P040 Test-M0Process requires a clean PowerShell process; preloaded module(s) '$loadedNames' were preserved and no module was removed or reloaded.")
}
foreach ($nativeTypeName in @(
    'M0ProcessNativeV1.PilotSession',
    'M0ProcessNativeV1.ProcessCapability',
    'M0ProcessNativeV1.TrustedFile'
)) {
    if ($nativeTypeName -as [type]) {
        throw [IO.InvalidDataException]::new("M0P040 Test-M0Process requires a clean PowerShell process; preloaded native type '$nativeTypeName' is forbidden before trusted module import.")
    }
}

function Import-M0ProcessExactEntrypointModule {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $ImportPath,
        [Parameter(Mandatory)] [string] $ExpectedLoadedPath
    )

    $expected = [IO.Path]::GetFullPath($ExpectedLoadedPath)
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (@(Get-Module -Name $Name).Count -ne 0) {
        throw [IO.InvalidDataException]::new("M0P040 module '$Name' appeared after the clean-process gate; no module was removed or reloaded.")
    }
    Import-Module -Name ([IO.Path]::GetFullPath($ImportPath)) -ErrorAction Stop
    $loaded = @(Get-Module -Name $Name)
    if ($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($expected, $comparison)) {
        throw [IO.InvalidDataException]::new("M0P040 module '$Name' did not bind to the exact repository path '$expected'.")
    }
    return $loaded[0]
}

$harnessRoot = Join-Path $repositoryRoot 'experiments\m0\harness'
$null = Import-M0ProcessExactEntrypointModule -Name 'M0.Harness' `
    -ImportPath (Join-Path $harnessRoot 'M0.Harness.psd1') -ExpectedLoadedPath (Join-Path $harnessRoot 'M0.Harness.psm1')

$protocolRoot = Join-Path $repositoryRoot 'experiments\m0\protocol'
$schemaRoot = Join-Path $protocolRoot 'schemas'
$protocol = M0.Harness\Assert-M0JsonDocument `
    -DocumentPath (Join-Path $protocolRoot 'protocol.v1.json') -SchemaPath (Join-Path $schemaRoot 'protocol.schema.json')
$null = M0.Harness\Assert-M0HarnessManifest `
    -RepositoryRoot $repositoryRoot -ExpectedManifestSha256 $protocol.harness_manifest_sha256
$null = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $repositoryRoot

$expectedProcessPath = [IO.Path]::GetFullPath((Join-Path $harnessRoot 'M0.Process.psm1'))
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$trustedProcess = @(Get-Module -All -Name 'M0.Process')
if ($trustedProcess.Count -ne 1 -or
    -not [IO.Path]::GetFullPath($trustedProcess[0].Path).Equals($expectedProcessPath, $comparison)) {
    throw [IO.InvalidDataException]::new("M0P040 M0.Process was not loaded from the exact trusted repository path '$expectedProcessPath'.")
}
$processModule = $trustedProcess[0]
$contract = & $processModule { param($root) Assert-M0ProcessArtifacts -ProtocolRoot $root } $protocolRoot

if (-not $IsWindows) {
    Write-Host "OK: $($contract.process_id); Linux live cleanup remains not_proven; no process-tree artifact was published or sealed."
    return
}

$pilot = $null
$snapshot = $null
$artifact = $null
$cleanup = $null
try {
    $pilot = & $processModule { param($root) Open-M0SyntheticProcessPilot -RepositoryRoot $root } $repositoryRoot
    $snapshot = & $processModule { param($capability) Get-M0SyntheticProcessPilotSnapshot -Pilot $capability } $pilot
    $artifact = & $processModule {
        param($tree)
        New-M0ProcessTreeArtifact -Snapshot $tree `
            -ExperimentId 'm0-local-process-smoke' -RunId 'run-P-synthetic-process-tree-01'
    } $snapshot
}
finally {
    if ($null -ne $pilot) {
        $cleanup = & $processModule { param($capability) Close-M0SyntheticProcessPilot -Pilot $capability } $pilot
    }
}

if ($snapshot.state -cne 'partial' -or $snapshot.error_code -cne 'etw_live_not_implemented') {
    throw [IO.InvalidDataException]::new('M0P100 the Windows Job smoke must remain partial until live ETW attribution is implemented.')
}
if (-not [bool]$cleanup.cleanup_complete -or [int]$cleanup.survivor_count -ne 0 -or
    [int]$cleanup.native_cleanup_actions -ne 1) {
    throw [IO.InvalidDataException]::new('M0P100 the Windows Job smoke did not prove one scoped Job close with zero survivors.')
}
$outcome = & $processModule {
    param($state, $processError, $cleanupComplete, $survivorCount, $cleanupError)
    Resolve-M0ProcessPilotOutcome -ProcessState $state -ProcessErrorCode $processError `
        -CleanupComplete $cleanupComplete -SurvivorCount $survivorCount -CleanupErrorCode $cleanupError
} $snapshot.state $snapshot.error_code $cleanup.cleanup_complete $cleanup.survivor_count $cleanup.error_code
if ($outcome.run_status -cne 'inconclusive' -or $outcome.run_status_reason -cne 'incomplete_evidence' -or
    $artifact.document.candidate_code -cne 'P' -or $artifact.document.decision_eligible -ne $false) {
    throw [IO.InvalidDataException]::new('M0P100 the in-memory synthetic P artifact/outcome violated its non-decision contract.')
}

Write-Host "OK: $($contract.process_id); Windows Job cleanup smoke passed with zero survivors; synthetic P artifact validated in memory, not published or sealed."
