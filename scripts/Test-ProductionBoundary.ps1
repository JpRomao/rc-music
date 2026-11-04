[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [ValidateSet('Lite', 'CompleteExperimental')][string]$Edition = 'Lite'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Import-Module (Join-Path $repository 'packaging/windows/RcMusicRuntimePlan.psm1') -Force

# Inspect the configured dependency graph, not stale object files or a test build.
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
function Get-CacheValue([string]$Name) {
    $entries = @($cache | Where-Object { $_ -match ('^' + [regex]::Escape($Name) + ':[^=]+=') })
    if ($entries.Count -ne 1) { throw "Expected exactly one CMake cache entry: $Name" }
    return ($entries[0] -split '=', 2)[1]
}

if ((Get-CacheValue 'CMAKE_GENERATOR') -cne 'Ninja') { throw 'Unsupported build configuration: Ninja is required' }
$buildCache = @{}
foreach ($line in $cache) {
    if ($line -match '^([^#/][^:]*):[^=]+=(.*)$') {
        if ($buildCache.ContainsKey($Matches[1])) { throw "Duplicate CMake cache entry: $($Matches[1])" }
        $buildCache[$Matches[1]] = $Matches[2]
    }
}
$configuration = Get-RcMusicDesktopBuildConfiguration -Cache $buildCache -RepositoryRoot $repository -BuildTesting 'OFF' -Edition $Edition

$ninja = Get-CacheValue 'CMAKE_MAKE_PROGRAM'
if (-not (Test-Path -LiteralPath $ninja -PathType Leaf)) { throw 'Configured Ninja executable is missing' }
$commands = @(& $ninja -C $buildRoot -t commands $configuration.executable)
if ($LASTEXITCODE -ne 0 -or $commands.Count -eq 0) { throw 'Could not inspect the executable dependency graph' }
$graph = $commands -join "`n"
$browserSources = @('WindowsWebView2Browser.cpp', 'WindowsWebView2Options.cpp', 'BrowserMediaPanel.cpp')
$requiredSources = @('PlayerPresenter.cpp', 'LocalQueueListModel.cpp', 'QtMultimediaPlaybackBackend.cpp')
if ($configuration.edition -ceq 'CompleteExperimental') { $requiredSources += $browserSources }
foreach ($source in $requiredSources) {
    if ($graph -notmatch ('[\\/]' + [regex]::Escape($source) + '(?:["\s]|$)')) {
        throw "Real application source is missing from the graph: $source"
    }
}
if ($graph -notmatch '(?i)/out:m0_qt_shell\.exe(?:\s|$)') { throw 'The expected executable link command is missing' }

# Link response files hide library arguments from `-t commands`. Query both
# editions and inspect only dependencies of the exact executable input section.
$query = @(& $ninja -C $buildRoot -t query $configuration.executable)
if ($LASTEXITCODE -ne 0 -or $query.Count -eq 0) { throw 'Could not inspect the executable dependency query' }
if ($query[0].Trim() -cne ($configuration.executable + ':')) { throw 'Unexpected executable dependency query target' }
$dependencies = [Collections.Generic.List[string]]::new()
$inInputs = $false
$sawInputs = $false
foreach ($line in @($query | Select-Object -Skip 1)) {
    if ($line -cmatch '^  input:\s*\S') {
        if ($sawInputs) { throw 'Duplicate executable input section in dependency query' }
        $inInputs = $true
        $sawInputs = $true
        continue
    }
    if ($line -cmatch '^  \S') { $inInputs = $false }
    if ($inInputs -and $line -cmatch '^    (?:\|{1,2} )?(.+)$') {
        $dependencies.Add($Matches[1].Trim())
    }
}
if (-not $sawInputs -or $dependencies.Count -eq 0) { throw 'Executable dependency query has no input dependencies' }
$hasBrowserLoader = @($dependencies | Where-Object {
    $_ -match '(?i)(?:^|[\\/])WebView2Loader(?:Static)?\.(?:lib|dll)$'
}).Count -gt 0
if ($configuration.edition -ceq 'CompleteExperimental') {
    $expectedLoader = (Join-Path $configuration.browser_sdk_root 'build/native/x64/WebView2LoaderStatic.lib').Replace('\', '/')
    $matchingLoader = @($dependencies | Where-Object {
        [string]::Equals($_.Replace('\', '/'), $expectedLoader, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matchingLoader.Count -ne 1) { throw 'Complete browser static loader dependency from the configured SDK is missing' }
}

$violations = [System.Collections.Generic.List[string]]::new()
$fullGraph = $graph + "`n" + ($query -join "`n")
if ($configuration.edition -ceq 'Lite') {
    foreach ($source in $browserSources) {
        if ($graph -match ('[\\/]' + [regex]::Escape($source) + '(?:["\s]|$)')) {
            $violations.Add("Lite browser source is present in the graph: $source")
        }
    }
    if ($hasBrowserLoader -or $graph -match '(?i)(?:^|[\\/\s"])WebView2Loader(?:Static)?\.(?:lib|dll)(?:["\s]|$)') {
        $violations.Add('Lite browser loader is present in the executable dependency graph')
    }
}
if ($configuration.entrypoint -ceq 'product' -and $fullGraph -match '[\\/]experiments[\\/]') {
    $violations.Add('Historical source/include path: product compilation references experiments')
}
foreach ($source in @('FixtureRepository.cpp', 'QueueListModel.cpp', 'DeterministicPlaybackBackend.cpp')) {
    if ($fullGraph -match ('[\\/]' + [regex]::Escape($source) + '(?:["\s]|$)')) {
        $violations.Add("Historical source: $source")
    }
}
foreach ($pattern in @('rc_music_test_support', 'm0_qt_fixture_data', 'RC_MUSIC_ENABLE_FIXTURE_SCENARIOS', 'tests[\\/]support[\\/]include')) {
    if ($fullGraph -match $pattern) { $violations.Add("Historical dependency: $pattern") }
}
if ($violations.Count -ne 0) { throw ($violations -join "`n") }

Write-Output "OK: production graph inspected ($($commands.Count) commands), real sources present, historical sources/support absent."
