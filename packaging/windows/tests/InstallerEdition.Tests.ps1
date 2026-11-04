#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$MakeNsis)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$recipes = Join-Path $repository 'packaging/windows'
Import-Module (Join-Path $recipes 'RcMusicInstallerPlan.psm1') -Force
$compiler = (Resolve-Path -LiteralPath $MakeNsis).ProviderPath
$toolchain = Get-Content -LiteralPath (Join-Path $recipes 'nsis-toolchain.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant() -cne $toolchain.compilerSha256) {
    throw 'Edition preprocessor tests require the recorded NSIS compiler'
}
$parent = Join-Path $repository 'out/product/test-runs/installer-editions'
$id = [guid]::NewGuid().ToString('N')
$root = Join-Path $parent $id
$state = @{ Passed = 0; Failed = 0; Next = 0 }
function SafeRoot {
    if ($id -cnotmatch '^[0-9a-f]{32}$' -or [IO.Path]::GetFullPath($root) -cne [IO.Path]::GetFullPath((Join-Path $parent $id))) { throw 'Unsafe test root' }
    for ($path = $root; $path; $path = [IO.Path]::GetDirectoryName($path)) {
        try { $attributes = [IO.File]::GetAttributes($path) }
        catch [IO.FileNotFoundException] { continue }
        catch [IO.DirectoryNotFoundException] { continue }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse test ancestor' }
    }
}
function Case([string]$Name, [scriptblock]$Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Fixture([string]$Edition) {
    $state.Next++
    $base = Join-Path $root ([string]$state.Next)
    $app = Join-Path $base 'app'
    $null = [IO.Directory]::CreateDirectory($app)
    $files = @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt')
    if ($Edition -ceq 'CompleteExperimental') { $files += @('notices/WebView2-SDK-LICENSE.txt', 'notices/WebView2-SDK-NOTICE.txt', 'WEBVIEW2-DEVELOPMENT.txt') }
    $entries = @(foreach ($relative in $files) {
        $path = Join-Path $app $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Synthetic bytes for preprocessing only')
        [pscustomobject]@{ relative_path = $relative; size_bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $manifest = [ordered]@{ schema_version = 1; name = 'RC Music'; architecture = 'x64'; build_testing = $false
        recipe_dirty = $false; source_commit = ('1' * 40); distribution = 'private-local-development-only'
        edition = $Edition; browser_runtime_requirement = $(if ($Edition -ceq 'Lite') { 'none' } else { 'existing-webview2-evergreen-x64' }); files = $entries }
    [IO.File]::WriteAllText((Join-Path $base 'runtime-manifest.json'), ($manifest | ConvertTo-Json -Depth 6))
    $output = Join-Path $base 'never-compiled'
    $plan = New-RcMusicInstallerPlan -RuntimeDirectory $app -OutputDirectory $output -Edition $Edition
    return [pscustomobject]@{ Base = $base; Output = $output; Plan = $plan }
}
function Preprocess($Scenario, [string]$Append = '') {
    $config = Join-Path $Scenario.Base 'installer-config.nsh'
    [IO.File]::WriteAllText($config, $Scenario.Plan.config + $Append, [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new($compiler)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $recipes
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($argument in @('/SAFEPPO', '/V0', '/INPUTCHARSET', 'UTF8', '/OUTPUTCHARSET', 'UTF8',
        ('/DRCMUSIC_CONFIG=' + (ConvertTo-RcMusicNsisLiteral $config)), (Join-Path $recipes 'RcMusicInstaller.nsi'))) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw 'Could not start preprocessor' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { throw 'Preprocessor timeout' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Text = $stdout.GetAwaiter().GetResult(); ErrorText = $stderr.GetAwaiter().GetResult() }
    } finally {
        try { if ($started -and -not $process.HasExited) { $process.Kill($true); if (-not $process.WaitForExit(5000)) { throw 'Owned preprocessor survived cleanup' } } }
        finally { $process.Dispose() }
    }
}
SafeRoot
Require (-not (Test-Path -LiteralPath $root)) 'Test root already exists'
$null = [IO.Directory]::CreateDirectory($root)
try {
    Case 'Complete welcome explains its existing runtime prerequisite and replacement policy' {
        $scenario = Fixture CompleteExperimental
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        Require ($result.Text.Contains('Edição Completa experimental')) 'Complete edition missing from setup text'
        Require ($result.Text.Contains('WebView2 Evergreen x64 já instalado')) 'Existing runtime prerequisite missing'
        Require ($result.Text.Contains('Não baixa nem instala o runtime')) 'Runtime installation boundary missing'
        Require ($result.Text.Contains('substitui a versão atual')) 'Single installation replacement policy missing'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Preprocessor created installer output'
    }
    Case 'Lite welcome preserves local operation and shares the same product identity' {
        $scenario = Fixture Lite
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        Require ($result.Text.Contains('Edição Lite')) 'Lite edition missing from setup text'
        Require (-not $result.Text.Contains('WebView2 Evergreen x64 já instalado')) 'Lite unexpectedly requires WebView2'
        Require ($result.Text.Contains('substitui a versão atual')) 'Edition replacement policy missing'
        Require ($result.Text.Contains('Name "RC Music"')) 'Product identity changed'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Preprocessor created installer output'
    }
    Case 'NSIS refuses inconsistent edition and browser prerequisite metadata' {
        $scenario = Fixture CompleteExperimental
        $result = Preprocess $scenario "`n!define /redef RCMUSIC_REQUIRES_WEBVIEW2 0`n"
        Require ($result.ExitCode -ne 0) 'Inconsistent edition metadata was accepted'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Rejected metadata created output'
    }
} finally {
    SafeRoot
    foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse child refused during cleanup' }
    }
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($root)) -Recurse -Force
}
Write-Host "Installer edition preprocessor: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed) { exit 1 }
