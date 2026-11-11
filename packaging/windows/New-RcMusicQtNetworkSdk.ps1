#Requires -Version 7.0
[CmdletBinding()]
param(
    [string] $VendorSdkRoot = 'q/f75eb9bea8280844c6d1a2b3f4/sdk',
    [string] $DllDirectory = 'q/n2/b/bin',
    [string] $OutputDirectory = 'out/product/qt-network-sdk'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vendor = [IO.Path]::GetFullPath($VendorSdkRoot, $repository)
$dllDirectory = [IO.Path]::GetFullPath($DllDirectory, $repository)
$output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
$buildRoot = (Join-Path $repository 'out/product').TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $output.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Qt SDK output must be a fresh directory below repository out/product'
}
$configRelative = 'lib/cmake/Qt6/Qt6Config.cmake'
if (-not [IO.File]::Exists((Join-Path $vendor $configRelative))) { throw 'Qt vendor package config is missing' }
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1')
Import-Module (Join-Path $PSScriptRoot 'RcMusicQtNetworkRuntime.psm1')

$qualified = Get-RcMusicQtNetworkRuntime -ManifestPath (Join-Path $PSScriptRoot 'qt-network-runtime.v1.json') `
    -DllDirectory $dllDirectory -Destination $output
$relativeDll = 'bin/Qt6Network.dll'
$relativeSymbols = 'bin/Qt6Network.pdb'
$mappings = foreach ($entry in Get-ChildItem -LiteralPath $vendor -Recurse -Force) {
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Qt vendor SDK contains a reparse point' }
    if ($entry.PSIsContainer) { continue }
    $relative = [IO.Path]::GetRelativePath($vendor, $entry.FullName).Replace('\', '/')
    $source = if ($relative -ceq $relativeDll) { $qualified.source }
        elseif ($relative -ceq $relativeSymbols) { Join-Path $dllDirectory 'Qt6Network.pdb' }
        else { $entry.FullName }
    @{ source = $source; relative_path = $relative }
}
$plan = New-RcMusicRuntimePlan -Files $mappings -AllowedSourceRoots @($vendor, $dllDirectory) -Destination $output
$entry = @($plan.files | Where-Object relative_path -CEQ $relativeDll)
if ($entry.Count -ne 1 -or $entry[0].sha256 -cne $qualified.sha256 -or $entry[0].size_bytes -ne $qualified.size_bytes) {
    throw 'Qt SDK plan differs from its qualified Qt6Network.dll input'
}
$null = Copy-RcMusicRuntimePlan -Plan $plan
[pscustomobject]@{
    QtPrefix = $plan.destination
    qt_network_sha256 = $qualified.sha256
    files = $plan.files.Count
}
