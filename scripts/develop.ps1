#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('format', 'check', 'build', 'test', 'lint', 'validate')][string] $Task = 'validate',
    [string] $ToolchainFile,
    [string] $BuildDirectory,
    [ValidateRange(1, 16)][int] $Jobs = 4,
    [switch] $Apply,
    [switch] $Diagnostics
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
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
$context = $null
$preflight = $null
$failure = $null
try {
    if ($Diagnostics) {
        Import-Module (Join-Path $PSScriptRoot 'DevelopmentDiagnostics.psm1') -Force
        $context = New-RcMusicDevelopmentDiagnostics -RepositoryRoot $repository -Task $Task -BuildDirectory $BuildDirectory
        Write-Host "Diagnostics: $($context.Directory)"
        $preflight = Start-RcMusicDiagnosticStep -Context $context -Name preflight
    }
    if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
    if ($Apply -and $Task -ne 'format') { throw 'Apply is only valid for format' }
    $toolchainHash = if ($context) { (Get-FileHash -LiteralPath $ToolchainFile -Algorithm SHA256 -ErrorAction Stop).Hash } else { $null }
    $tools = Read-RcMusicDevelopmentTools -Path $ToolchainFile -RepositoryRoot $repository -Task $Task
    if ($context) {
        if ((Get-FileHash -LiteralPath $ToolchainFile -Algorithm SHA256 -ErrorAction Stop).Hash -cne $toolchainHash) {
            throw 'Toolchain configuration changed while being read'
        }
        $context.Report.toolchainFile = [IO.Path]::GetFullPath($ToolchainFile)
        $context.Report.toolchainSha256 = $toolchainHash.ToLowerInvariant()
        $context.Report.selectedTools = [ordered]@{}
        foreach ($key in ($tools.Keys | Sort-Object)) { $context.Report.selectedTools[$key] = $tools[$key] }
    }
    if ($Task -in @('build', 'test', 'lint', 'validate')) {
        $testing = if ($Task -eq 'build') { 'OFF' } else { 'ON' }
        Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory $BuildDirectory -Testing $testing -Tools $tools
        $packaging = Join-Path $repository 'packaging/windows'
        Import-Module (Join-Path $packaging 'RcMusicFfmpegRuntime.psm1')
        $multimedia = [IO.Path]::GetFullPath((Join-Path $tools.QtMultimediaPackage '../../..'))
        $preflightDestination = Join-Path $BuildDirectory ('.runtime-preflight-' + [guid]::NewGuid().ToString('N'))
        $null = Get-RcMusicFfmpegRuntime -ManifestPath (Join-Path $packaging 'ffmpeg-runtime.v1.json') `
            -DllDirectory (Join-Path $multimedia 'bin') `
            -PluginPath (Join-Path $multimedia 'plugins/multimedia/ffmpegmediaplugin.dll') -Destination $preflightDestination
        Import-Module (Join-Path $packaging 'RcMusicQtNetworkRuntime.psm1')
        $null = Get-RcMusicQtNetworkRuntime -ManifestPath (Join-Path $packaging 'qt-network-runtime.v1.json') `
            -DllDirectory (Join-Path $tools.QtPrefix 'bin') -Destination $preflightDestination
        $devShell = Join-Path $tools.VisualStudioRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
        if (-not (Test-Path -LiteralPath $devShell -PathType Leaf)) { throw 'Visual Studio developer shell is unavailable' }
        Import-Module $devShell
        Enter-VsDevShell -VsInstallPath $tools.VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0'
    }
    if ($preflight) { Complete-RcMusicDiagnosticStep -Context $context -Step $preflight -ExitCode 0 }
    $runDirectory = if ($context) { $context.Directory } else { '' }
    $plan = @(Get-RcMusicDevelopmentPlan -Task $Task -RepositoryRoot $repository -BuildDirectory $BuildDirectory -Tools $tools -Jobs $Jobs -Apply:$Apply -DiagnosticsDirectory $runDirectory)
    Invoke-RcMusicDevelopmentPlan -Plan $plan -DiagnosticsContext $context
} catch {
    $failure = $_
    if ($preflight -and $preflight.status -ceq 'running') {
        try { Complete-RcMusicDiagnosticStep -Context $context -Step $preflight -Failure $failure.Exception.Message }
        catch { Write-Warning "Could not preserve preflight diagnostic: $($_.Exception.Message)" -WarningAction Continue }
    }
    throw $failure
} finally {
    if ($context) {
        try { Complete-RcMusicDevelopmentDiagnostics -Context $context -Failed:($null -ne $failure) }
        catch {
            if ($null -eq $failure) { throw }
            Write-Warning "Could not finalize diagnostics: $($_.Exception.Message)" -WarningAction Continue
        }
    }
}
Write-Host "RC Music $Task passed."
