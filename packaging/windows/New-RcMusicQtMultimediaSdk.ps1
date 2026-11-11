#Requires -Version 7.0
[CmdletBinding()]
param(
    [string] $VendorSdkRoot = 'out/m0/qt-multimedia-archive-probe-20260829-01/stage',
    [string] $FfmpegBinDirectory = 'out/product/fffix2/install/bin',
    [string] $OutputDirectory = 'out/product/qt-multimedia-6.11.2-rcmusic2'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vendor = [IO.Path]::GetFullPath($VendorSdkRoot, $repository)
$dllDirectory = [IO.Path]::GetFullPath($FfmpegBinDirectory, $repository)
$output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
$buildRoot = (Join-Path $repository 'out/product').TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $output.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase) -or $output -eq $buildRoot) {
    throw 'Qt Multimedia SDK output must be a fresh directory below repository out/product'
}
$configRelative = 'lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake'
if (-not [IO.File]::Exists((Join-Path $vendor $configRelative))) { throw 'Qt Multimedia vendor package config is missing' }
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1')
Import-Module (Join-Path $PSScriptRoot 'RcMusicFfmpegRuntime.psm1')

$plugin = 'plugins/multimedia/ffmpegmediaplugin.dll'
$qualified = Get-RcMusicFfmpegRuntime -ManifestPath (Join-Path $PSScriptRoot 'ffmpeg-runtime.v1.json') `
    -DllDirectory $dllDirectory -PluginPath (Join-Path $vendor $plugin) -Destination $output
$required = @{}
foreach ($file in $qualified.files) {
    $relative = if ($file.relative_path -ceq $plugin) { $plugin } else { 'bin/' + $file.relative_path }
    $required.Add($relative, $file)
}
$mappings = foreach ($entry in Get-ChildItem -LiteralPath $vendor -Recurse -Force) {
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Qt Multimedia vendor SDK contains a reparse point' }
    if ($entry.PSIsContainer) { continue }
    $relative = [IO.Path]::GetRelativePath($vendor, $entry.FullName).Replace('\', '/')
    $source = if ($required.ContainsKey($relative)) { $required[$relative].source } else { $entry.FullName }
    @{ source = $source; relative_path = $relative }
}
$plan = New-RcMusicRuntimePlan -Files $mappings -AllowedSourceRoots @($vendor, $dllDirectory) -Destination $output
foreach ($relative in $required.Keys) {
    $expected = $required[$relative]
    $collected = @($plan.files | Where-Object relative_path -CEQ $relative)
    if ($collected.Count -ne 1 -or $collected[0].size_bytes -ne $expected.size_bytes -or
        $collected[0].sha256 -cne $expected.sha256) { throw "Qt Multimedia SDK plan differs from its qualified input: $relative" }
}
$null = Copy-RcMusicRuntimePlan -Plan $plan
[pscustomobject]@{
    sdk = $plan.destination
    QtMultimediaPackage = [IO.Path]::GetDirectoryName((Join-Path $plan.destination $configRelative))
    ffmpeg_version = $qualified.product_version
}
