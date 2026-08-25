[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $EvidenceRoot,
    [Parameter(Mandatory)] [string] $ExperimentId,
    [Parameter(Mandatory)] [string] $RunId,
    [AllowEmptyString()] [string] $CrashAfterBoundary = '',
    [switch] $AbruptCrash
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-P requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$entrypointModuleNames = @(
    'M0.Harness', 'M0.Pilot', 'M0.Summary', 'M0.Fixture', 'M0.Markers',
    'M0.Statistics', 'M0.Visual', 'M0.Process', 'M0.Evidence'
)
$preloadedModules = @(Get-Module -All -Name $entrypointModuleNames)
if ($preloadedModules.Count -ne 0) {
    $loadedNames = @($preloadedModules | ForEach-Object Name | Sort-Object -Unique) -join ', '
    throw [IO.InvalidDataException]::new(
        "M0I040 Invoke-M0Pilot requires a clean PowerShell process; preloaded module(s) '$loadedNames' were preserved and no module was removed or reloaded."
    )
}
foreach ($nativeTypeName in @(
    'M0ProcessNativeV1.PilotSession',
    'M0ProcessNativeV1.ProcessCapability',
    'M0ProcessNativeV1.TrustedFile',
    'M0EvidenceNativeV1.WindowsFileIdentity',
    'M0EvidenceNativeV1.WindowsFileFingerprint'
)) {
    if ($nativeTypeName -as [type]) {
        throw [IO.InvalidDataException]::new(
            "M0I040 Invoke-M0Pilot requires a clean PowerShell process; preloaded native type '$nativeTypeName' is forbidden before trusted module import."
        )
    }
}

function Import-M0PilotExactEntrypointModule {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $ImportPath,
        [Parameter(Mandatory)] [string] $ExpectedLoadedPath
    )

    $expected = [IO.Path]::GetFullPath($ExpectedLoadedPath)
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (@(Get-Module -All -Name $Name).Count -ne 0) {
        throw [IO.InvalidDataException]::new(
            "M0I040 module '$Name' appeared after the clean-process gate; no module was removed or reloaded."
        )
    }
    Import-Module -Name ([IO.Path]::GetFullPath($ImportPath)) -ErrorAction Stop
    $loaded = @(Get-Module -All -Name $Name)
    if ($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($expected, $comparison)) {
        throw [IO.InvalidDataException]::new("M0I040 module '$Name' did not bind to the exact repository path '$expected'.")
    }
    return $loaded[0]
}

$harnessRoot = Join-Path $repositoryRoot 'experiments\m0\harness'
$null = Import-M0PilotExactEntrypointModule -Name 'M0.Harness' `
    -ImportPath (Join-Path $harnessRoot 'M0.Harness.psd1') `
    -ExpectedLoadedPath (Join-Path $harnessRoot 'M0.Harness.psm1')
$trusted = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $repositoryRoot `
    -Scenario 'synthetic-process-tree' -LoadPilot

$expectedPilotPath = [IO.Path]::GetFullPath((Join-Path $harnessRoot 'M0.Pilot.psm1'))
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$pilotModules = @(Get-Module -All -Name 'M0.Pilot')
if ($pilotModules.Count -ne 1 -or
    -not [IO.Path]::GetFullPath($pilotModules[0].Path).Equals($expectedPilotPath, $comparison)) {
    throw [IO.InvalidDataException]::new(
        "M0I040 M0.Pilot was not retained from the manifest-locked exact repository path '$expectedPilotPath'."
    )
}

$parameters = @{
    RepositoryRoot = $repositoryRoot
    EvidenceRoot = $EvidenceRoot
    ExperimentId = $ExperimentId
    RunId = $RunId
    ExpectedProtocolSha256 = [string]$trusted.protocol_sha256
    CrashAfterBoundary = $CrashAfterBoundary
    AbruptCrash = $AbruptCrash
}
& $pilotModules[0] { param($arguments) Invoke-M0SyntheticPilot @arguments } $parameters
