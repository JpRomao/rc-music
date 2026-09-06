#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('format', 'check', 'build', 'test', 'lint', 'validate')][string] $Task = 'validate',
    [string] $ToolchainFile,
    [string] $BuildDirectory,
    [ValidateRange(1, 16)][int] $Jobs = 4,
    [switch] $Apply
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
if ($Apply -and $Task -ne 'format') { throw 'Apply is only valid for format' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Import-Module (Join-Path $PSScriptRoot 'DevelopmentCommands.psm1') -Force
if (-not $ToolchainFile) { $ToolchainFile = Join-Path $repository '.tools/windows.psd1' }
if (-not [IO.Path]::IsPathFullyQualified($ToolchainFile)) { $ToolchainFile = Join-Path $repository $ToolchainFile }
if (-not $BuildDirectory) {
    $directory = if ($Task -eq 'build') { 'out/product/desktop-release' } else { 'out/product/desktop-tests' }
    $BuildDirectory = Join-Path $repository $directory
}
if (-not [IO.Path]::IsPathFullyQualified($BuildDirectory)) { $BuildDirectory = Join-Path $repository $BuildDirectory }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$tools = Read-RcMusicDevelopmentTools -Path $ToolchainFile -RepositoryRoot $repository -Task $Task
if ($Task -in @('build', 'test', 'lint', 'validate')) {
    $testing = if ($Task -eq 'build') { 'OFF' } else { 'ON' }
    Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory $BuildDirectory -Testing $testing -Tools $tools
    $devShell = Join-Path $tools.VisualStudioRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path -LiteralPath $devShell -PathType Leaf)) { throw 'Visual Studio developer shell is unavailable' }
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $tools.VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0'
}
$plan = @(Get-RcMusicDevelopmentPlan -Task $Task -RepositoryRoot $repository -BuildDirectory $BuildDirectory -Tools $tools -Jobs $Jobs -Apply:$Apply)
Invoke-RcMusicDevelopmentPlan -Plan $plan
Write-Host "RC Music $Task passed."
