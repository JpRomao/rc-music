#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $RuntimeDirectory,
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $InstrumentedBuildDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $PSScriptRoot '../RcMusicRuntimePlan.psm1') -Force
$RuntimeDirectory = (Resolve-Path -LiteralPath $RuntimeDirectory).ProviderPath
$productionExe = Join-Path $RuntimeDirectory 'RCMusic.exe'
foreach ($relative in @('RCMusic.exe', 'qt.conf', 'plugins/platforms/qoffscreen.dll',
        'plugins/multimedia/ffmpegmediaplugin.dll', 'qml/QtQuick/Controls/Basic/qmldir')) {
    if (-not [IO.File]::Exists((Join-Path $RuntimeDirectory $relative))) {
        throw "Missing runtime file: $relative"
    }
}
$InstrumentedBuildDirectory = (Resolve-Path -LiteralPath $InstrumentedBuildDirectory).ProviderPath
$cache = @{}
foreach ($line in (Get-Content -LiteralPath (Join-Path $InstrumentedBuildDirectory 'CMakeCache.txt'))) {
    if ($line -match '^([^#/][^:]*):[^=]+=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
}
$configuration = Get-RcMusicDesktopBuildConfiguration -Cache $cache -RepositoryRoot $repository -BuildTesting 'ON'
$instrumentedExe = Join-Path $InstrumentedBuildDirectory $configuration.instrumented_executable
if (-not [IO.File]::Exists($instrumentedExe)) { throw 'Missing instrumented shell for the selected entrypoint' }
$audioIntegrationExe = Join-Path $InstrumentedBuildDirectory $configuration.audio_integration_executable
if (-not [IO.File]::Exists($audioIntegrationExe)) { throw 'Missing audio integration test for the selected entrypoint' }

$testRoot = Join-Path $repository ('out/product/test-runs/rc-music-runtime-smoke/' + [Guid]::NewGuid().ToString('N'))
$probeDirectory = Join-Path $testRoot 'probe relocatable 日本'
$probeExe = Join-Path $probeDirectory 'm0_qt_shell.exe'
$rejectedConfigRoot = Join-Path $testRoot 'production-config-not-created'
$probeConfigRoot = Join-Path $testRoot 'instrumented-config'
$temporaryDirectory = Join-Path $testRoot 'temp'
$profileDirectory = Join-Path $testRoot 'profile'
$roamingDirectory = Join-Path $profileDirectory 'AppData/Roaming'
$localDirectory = Join-Path $profileDirectory 'AppData/Local'
if (Test-Path -LiteralPath $testRoot) { throw 'Smoke directory already exists; refusing to reuse state' }
if ($probeDirectory.StartsWith($RuntimeDirectory.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw 'Instrumented probe must be outside the runtime payload' }
# Reuse the runtime path/hash boundary before the first write, including every
# existing ancestor of the fresh probe directory. This is not hostile-writer isolation.
$probePlan = New-RcMusicRuntimePlan -Files @(@{ source = $instrumentedExe; relative_path = 'm0_qt_shell.exe' }) `
    -AllowedSourceRoots @($InstrumentedBuildDirectory) -Destination $probeDirectory
$null = Copy-RcMusicRuntimePlan -Plan $probePlan
$null = New-Item -ItemType Directory -Path $temporaryDirectory, $roamingDirectory, $localDirectory
if ((Get-FileHash -LiteralPath $instrumentedExe -Algorithm SHA256).Hash -cne
        (Get-FileHash -LiteralPath $probeExe -Algorithm SHA256).Hash) { throw 'Instrumented copy hash mismatch' }
$productionHash = (Get-FileHash -LiteralPath $productionExe -Algorithm SHA256).Hash
$windowsDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
$systemDirectory = [Environment]::SystemDirectory

function Invoke-IsolatedRuntimeProcess([string] $Program, [string] $ConfigRoot,
    [string[]] $Arguments = @(), [ValidateRange(1, 60000)] [int] $TimeoutMilliseconds = 20000,
    [switch] $AllowQTestOutput) {
    $start = [Diagnostics.ProcessStartInfo]::new($Program)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $probeDirectory
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ($AllowQTestOutput) {
        foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    } else {
        $start.ArgumentList.Add('--m0-smoke=combined-startup')
        $start.ArgumentList.Add('--m0-test-config-root=' + $ConfigRoot)
    }

    # Do not inherit QTDIR, QT_DIR, QT_QPA_PLATFORM_PLUGIN_PATH, CMAKE_PREFIX_PATH,
    # QML/Qt/FFmpeg overrides or SDK paths. Only the declared payload supplies Qt.
    $start.Environment.Clear()
    $start.Environment['SystemRoot'] = $windowsDirectory
    $start.Environment['WINDIR'] = $windowsDirectory
    $start.Environment['SystemDrive'] = [IO.Path]::GetPathRoot($windowsDirectory).TrimEnd('\')
    $start.Environment['COMSPEC'] = Join-Path $systemDirectory 'cmd.exe'
    $start.Environment['PATH'] = @($RuntimeDirectory, $systemDirectory, $windowsDirectory) -join ';'
    $start.Environment['TEMP'] = $temporaryDirectory
    $start.Environment['TMP'] = $temporaryDirectory
    # These are additional hygiene, not a guarantee about Windows Known Folder
    # APIs. The ON hook uses ConfigRoot; OFF must reject before opening config.
    $start.Environment['USERPROFILE'] = $profileDirectory
    $start.Environment['APPDATA'] = $roamingDirectory
    $start.Environment['LOCALAPPDATA'] = $localDirectory
    $start.Environment['QT_PLUGIN_PATH'] = Join-Path $RuntimeDirectory 'plugins'
    $start.Environment['QML_IMPORT_PATH'] = Join-Path $RuntimeDirectory 'qml'
    $start.Environment['QML2_IMPORT_PATH'] = Join-Path $RuntimeDirectory 'qml'
    $start.Environment['QT_QPA_PLATFORM'] = 'offscreen'
    $start.Environment['QSG_RHI_BACKEND'] = 'software'
    $start.Environment['QT_QUICK_BACKEND'] = 'software'
    $start.Environment['QT_QUICK_CONTROLS_STYLE'] = 'Basic'
    $start.Environment['QT_MEDIA_BACKEND'] = 'ffmpeg'
    $start.Environment['QT_LOGGING_TO_CONSOLE'] = '1'
    $start.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
    $start.Environment['QT_FATAL_WARNINGS'] = '1'
    $start.Environment['QT_QPA_FONTDIR'] = Join-Path $windowsDirectory 'Fonts'
    if ($AllowQTestOutput) {
        # Match the local-audio.integration CTest contract: backend diagnostics
        # are visible in QTest output, not promoted to process-wide aborts.
        $null = $start.Environment.Remove('QT_FATAL_WARNINGS')
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $stdout = [IO.MemoryStream]::new()
    $stderr = [IO.MemoryStream]::new()
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw 'Could not start isolated runtime smoke' }
        $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
        if (-not $process.WaitForExit($TimeoutMilliseconds)) {
            throw "Runtime smoke exceeded its $TimeoutMilliseconds ms deadline"
        }
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]] @($stdoutTask, $stderrTask), 5000)) {
            throw 'Runtime smoke output streams did not close after exit'
        }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            StdoutBytes = $stdout.Length
            StderrBytes = $stderr.Length
            ProcessId = $process.Id
            TestOutput = if ($AllowQTestOutput) {
                [Text.Encoding]::UTF8.GetString($stdout.ToArray()) + [Text.Encoding]::UTF8.GetString($stderr.ToArray())
            } else { '' }
        }
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                # Retained process identity: never stop an unrelated application.
                $process.Kill($true)
                if (-not $process.WaitForExit(5000)) { throw 'Owned smoke process survived cleanup' }
            }
        } finally {
            $process.Dispose()
            $stdout.Dispose()
            $stderr.Dispose()
        }
    }
}

function Assert-SmokeResult($Result, [int] $ExpectedExit, [string] $Name) {
    if ($Result.ExitCode -ne $ExpectedExit -or $Result.StdoutBytes -ne 0 -or $Result.StderrBytes -ne 0) {
        throw "$Name expected exit $ExpectedExit and empty streams; got exit $($Result.ExitCode), stdout $($Result.StdoutBytes) B, stderr $($Result.StderrBytes) B"
    }
}

$production = Invoke-IsolatedRuntimeProcess $productionExe $rejectedConfigRoot
Assert-SmokeResult $production 1 'Production hook rejection'
if (Test-Path -LiteralPath $rejectedConfigRoot) { throw 'Production executable created the rejected configuration root' }
Write-Host '[PASS] Production payload rejects test hooks: exit 1, stdout/stderr 0 B, configuration root absent'

# No DLL, plugin or QML tree accompanies this copied executable. Startup must
# resolve them from the declared runtime. The hook checks the real empty state,
# requestExit, presenter shutdown and closed window before returning success.
$instrumented = Invoke-IsolatedRuntimeProcess $probeExe $probeConfigRoot
Assert-SmokeResult $instrumented 0 'Relocated instrumented combined startup'
if (-not [IO.Directory]::Exists($probeConfigRoot)) { throw 'Instrumented smoke did not create its isolated configuration root' }
if ((Get-FileHash -LiteralPath $productionExe -Algorithm SHA256).Hash -cne $productionHash) {
    throw 'Production executable changed during runtime smoke'
}
Write-Host '[PASS] Relocated combined startup uses the declared runtime and exits cleanly: exit 0, stdout/stderr 0 B'

$probeAudioExe = Join-Path $probeDirectory 'm0_qt_local_audio_integration_tests.exe'
$probeQtTest = Join-Path $probeDirectory 'Qt6Test.dll'
$manifestPath = Join-Path ([IO.Path]::GetDirectoryName($RuntimeDirectory.TrimEnd('\', '/'))) 'runtime-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$coreEntries = @($manifest.files | Where-Object { $_.relative_path -ceq 'Qt6Core.dll' })
if ($coreEntries.Count -ne 1) { throw 'Runtime manifest must identify exactly one Qt6Core.dll origin' }
$coreEntry = $coreEntries[0]
if (-not [IO.Path]::IsPathFullyQualified($coreEntry.source) -or
        [IO.Path]::GetFileName($coreEntry.source) -cne 'Qt6Core.dll') { throw 'Invalid Qt Core origin in runtime manifest' }
foreach ($coreFile in @($coreEntry.source, (Join-Path $RuntimeDirectory 'Qt6Core.dll'))) {
    if ((Get-FileHash -LiteralPath $coreFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne $coreEntry.sha256) {
        throw 'Qt Core source or payload no longer matches the runtime manifest'
    }
}
# Qt Test is harness-only. Resolve it from the recorded Qt Core origin, never
# from PATH, a guessed SDK or the instrumented build's possibly different Qt.
$qtTestSource = Join-Path ([IO.Path]::GetDirectoryName($coreEntry.source)) 'Qt6Test.dll'
if (-not [IO.File]::Exists($qtTestSource)) { throw 'Qt6Test.dll is missing beside the recorded Qt Core origin' }
foreach ($copy in @(@{ Source = $audioIntegrationExe; Target = $probeAudioExe },
        @{ Source = $qtTestSource; Target = $probeQtTest })) {
    if (Test-Path -LiteralPath $copy.Target) { throw 'Codec probe destination already exists' }
    Copy-Item -LiteralPath $copy.Source -Destination $copy.Target
    if ((Get-FileHash -LiteralPath $copy.Source -Algorithm SHA256).Hash -cne
            (Get-FileHash -LiteralPath $copy.Target -Algorithm SHA256).Hash) { throw 'Codec probe copy hash mismatch' }
}
if (-not [IO.File]::Exists($probeAudioExe) -or -not [IO.File]::Exists($probeQtTest)) {
    throw 'Missing isolated codec probe executable or Qt6Test.dll'
}
$audio = Invoke-IsolatedRuntimeProcess -Program $probeAudioExe -AllowQTestOutput -TimeoutMilliseconds 60000 `
    -Arguments @('opensUnicodeLocalAudioAndDrivesTheRealJourney', '-o', '-,txt')
Write-Host $audio.TestOutput
if ($audio.ExitCode -ne 0 -or
        $audio.TestOutput -cnotmatch '(?m)^Totals: 18 passed, 0 failed, 0 skipped, 0 blacklisted, [0-9]+ms\r?$' -or
        $audio.TestOutput -cmatch '(?m)^(FAIL!|SKIP)\s*:') {
    throw "Packaged-runtime codec journey did not pass all sixteen rows without skips (exit $($audio.ExitCode))"
}
if ((Get-FileHash -LiteralPath $productionExe -Algorithm SHA256).Hash -cne $productionHash) {
    throw 'Production executable changed during codec smoke'
}
Write-Host '[PASS] Sixteen synthetic codec rows: load/pause/50% seek/metadata/play from zero to EOF, 18 passed including setup/cleanup, zero skips'
Write-Host '[PASS] All three owned processes exited; test executables, Qt Test and state remain outside the payload'
$smokeResult = [pscustomobject]@{
    RuntimeDirectory = $RuntimeDirectory
    EvidenceDirectory = $testRoot
    Production = $production
    Instrumented = $instrumented
    Audio = $audio
}
[IO.File]::WriteAllText((Join-Path $testRoot 'smoke-result.json'),
    ($smokeResult | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
$smokeResult
