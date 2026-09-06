[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Inspect the configured dependency graph, not stale object files or a test build.
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt')
function Get-CacheValue([string]$Name) {
    $entries = @($cache | Where-Object { $_ -match ('^' + [regex]::Escape($Name) + ':[^=]+=') })
    if ($entries.Count -ne 1) { throw "Expected exactly one CMake cache entry: $Name" }
    return ($entries[0] -split '=', 2)[1]
}

foreach ($requirement in @{
    CMAKE_GENERATOR = 'Ninja'
    CMAKE_BUILD_TYPE = 'Release'
    BUILD_TESTING = 'OFF'
    M0_QT_SHELL_SUPPORT = 'ON'
    M0_QT_AUDIO_SUPPORT = 'ON'
}.GetEnumerator()) {
    if ((Get-CacheValue $requirement.Key) -cne $requirement.Value) {
        throw "Unsupported build configuration: $($requirement.Key) must be $($requirement.Value)"
    }
}

$ninja = Get-CacheValue 'CMAKE_MAKE_PROGRAM'
if (-not (Test-Path -LiteralPath $ninja -PathType Leaf)) { throw 'Configured Ninja executable is missing' }
$commands = @(& $ninja -C $buildRoot -t commands m0_qt_shell.exe)
if ($LASTEXITCODE -ne 0 -or $commands.Count -eq 0) { throw 'Could not inspect the executable dependency graph' }
$graph = $commands -join "`n"
foreach ($source in @('PlayerPresenter.cpp', 'LocalQueueListModel.cpp', 'QtMultimediaPlaybackBackend.cpp')) {
    if ($graph -notmatch ('[\\/]' + [regex]::Escape($source) + '(?:["\s]|$)')) {
        throw "Real application source is missing from the graph: $source"
    }
}
if ($graph -notmatch '(?i)/out:m0_qt_shell\.exe(?:\s|$)') { throw 'The expected executable link command is missing' }

$violations = [System.Collections.Generic.List[string]]::new()
foreach ($source in @('FixtureRepository.cpp', 'QueueListModel.cpp', 'DeterministicPlaybackBackend.cpp')) {
    if ($graph -match ('[\\/]' + [regex]::Escape($source) + '(?:["\s]|$)')) {
        $violations.Add("Historical source: $source")
    }
}
foreach ($pattern in @('rc_music_test_support', 'm0_qt_fixture_data', 'RC_MUSIC_ENABLE_FIXTURE_SCENARIOS', 'tests[\\/]support[\\/]include')) {
    if ($graph -match $pattern) { $violations.Add("Historical dependency: $pattern") }
}
if ($violations.Count -ne 0) { throw ($violations -join "`n") }

Write-Output "OK: production graph inspected ($($commands.Count) commands), real sources present, historical sources/support absent."
