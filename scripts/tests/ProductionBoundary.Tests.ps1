#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testParent = Join-Path $repository 'out/product/test-runs/production-boundary'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
function Assert-TestRoot {
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) {
        throw 'Unsafe test target'
    }
    $cursor = [IO.Path]::GetFullPath($testRoot)
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or -not $item.PSIsContainer) {
                throw 'Test target contains a reparse point or file ancestor'
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}
Assert-TestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Refusing to reuse a test directory' }
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0; Next = 0 }

function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function New-Build([string] $Entrypoint = 'product', [string] $ExtraCommand = '', [string] $SourceRepository = $repository) {
    $state.Next++
    $directory = Join-Path $testRoot ('case-' + $state.Next)
    $null = New-Item -ItemType Directory -Path $directory
    $ninja = Join-Path $directory 'fake-ninja.ps1'
    [IO.File]::WriteAllText($ninja, @'
if (($args -join '|') -cne ('-C|' + $PSScriptRoot + '|-t|commands|m0_qt_shell.exe')) {
    throw 'Unexpected graph command'
}
Get-Content -LiteralPath (Join-Path $PSScriptRoot 'commands.txt')
$global:LASTEXITCODE = 0
'@)
    $commands = @(
        'cl.exe /c "C:/source/src/presentation/PlayerPresenter.cpp"',
        'cl.exe /c "C:/source/src/presentation/LocalQueueListModel.cpp"',
        'cl.exe /c "C:/source/src/adapters/QtMultimediaPlaybackBackend.cpp"',
        'link.exe /out:m0_qt_shell.exe Qt6Core.lib'
    )
    if ($ExtraCommand) { $commands += $ExtraCommand }
    [IO.File]::WriteAllLines((Join-Path $directory 'commands.txt'), $commands)
    $sourceHome = if ($Entrypoint -ceq 'product') { $SourceRepository }
        elseif ($Entrypoint -ceq 'legacy') { Join-Path $SourceRepository 'experiments/m0/candidates/qt/shell' }
        else { Join-Path $SourceRepository 'foreign-source' }
    $cache = @('CMAKE_GENERATOR:INTERNAL=Ninja', 'CMAKE_BUILD_TYPE:STRING=Release', 'BUILD_TESTING:BOOL=OFF',
        "CMAKE_HOME_DIRECTORY:INTERNAL=$sourceHome", "CMAKE_MAKE_PROGRAM:FILEPATH=$ninja")
    if ($Entrypoint -ceq 'product') { $cache += 'RC_MUSIC_BUILD_DESKTOP:BOOL=ON' }
    else { $cache += @('M0_QT_SHELL_SUPPORT:BOOL=ON', 'M0_QT_AUDIO_SUPPORT:BOOL=ON') }
    [IO.File]::WriteAllLines((Join-Path $directory 'CMakeCache.txt'), $cache)
    return $directory
}
function Assert-Rejected([string] $Directory, [string] $Pattern) {
    try { & (Join-Path $repository 'scripts/Test-ProductionBoundary.ps1') -BuildDirectory $Directory | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected boundary rejection'
}

try {
    Test-Case 'exported production guard runs without experiments from an unrelated working directory' {
        $export = Join-Path $testRoot 'export source 日本'
        $null = New-Item -ItemType Directory -Path (Join-Path $export 'scripts'), (Join-Path $export 'packaging/windows')
        Copy-Item -LiteralPath (Join-Path $repository 'scripts/Test-ProductionBoundary.ps1') -Destination (Join-Path $export 'scripts')
        Copy-Item -LiteralPath (Join-Path $repository 'packaging/windows/RcMusicRuntimePlan.psm1') -Destination (Join-Path $export 'packaging/windows')
        $build = New-Build -SourceRepository $export
        $start = [Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $start.WorkingDirectory = $testRoot
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($argument in @('-NoProfile', '-NonInteractive', '-File',
                (Join-Path $export 'scripts/Test-ProductionBoundary.ps1'), '-BuildDirectory', $build)) {
            $start.ArgumentList.Add($argument)
        }
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $start
        $started = $false
        try {
            $started = $process.Start()
            if (-not $started) { throw 'Could not start exported guard' }
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
            if (-not $process.WaitForExit(10000)) { throw 'Exported guard exceeded its deadline' }
            $output = $stdout.GetAwaiter().GetResult()
            $errorText = $stderr.GetAwaiter().GetResult()
            if ($process.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($errorText) -or
                $output -notmatch 'OK: production graph inspected \(4 commands\)') {
                throw "Exported guard failed: $output $errorText"
            }
            if (Test-Path -LiteralPath (Join-Path $export 'experiments')) { throw 'Export acquired an experimental dependency' }
        } finally {
            if ($started -and -not $process.HasExited) {
                $process.Kill($true)
                if (-not $process.WaitForExit(5000)) { throw 'Owned guard process survived cleanup' }
            }
            $process.Dispose()
        }
    }
    Test-Case 'accepts product OFF without M0 options or test dependencies' {
        $build = New-Build
        & (Join-Path $repository 'scripts/Test-ProductionBoundary.ps1') -BuildDirectory $build | Out-Null
    }
    Test-Case 'preserves the explicitly identified legacy OFF entrypoint' {
        $build = New-Build 'legacy' 'cl.exe /c "C:/source/experiments/m0/candidates/qt/shell/src/OtherAdapter.cpp"'
        & (Join-Path $repository 'scripts/Test-ProductionBoundary.ps1') -BuildDirectory $build | Out-Null
    }
    Test-Case 'rejects an unknown CMake entrypoint even with legacy options' {
        Assert-Rejected (New-Build 'unknown') 'entrypoint|build|configuration'
    }
    Test-Case 'rejects duplicate required cache entries instead of selecting the last value' {
        $build = New-Build
        $cachePath = Join-Path $build 'CMakeCache.txt'
        [IO.File]::AppendAllText($cachePath, "BUILD_TESTING:BOOL=OFF`n")
        Assert-Rejected $build 'duplicate|exactly one'
    }
    foreach ($command in @('cl.exe /c "C:/source/experiments/m0/candidates/qt/shell/src/OtherAdapter.cpp"',
            'cl.exe /I"C:\source\experiments\m0\shared\shell-fixture" /c C:/source/src/Real.cpp')) {
        Test-Case 'rejects experimental source or include paths in product OFF' {
            Assert-Rejected (New-Build 'product' $command) 'Historical|experimental'
        }
    }
    Test-Case 'continues rejecting deterministic fixture sources in legacy OFF' {
        Assert-Rejected (New-Build 'legacy' 'cl.exe /c "C:/source/FixtureRepository.cpp"') 'Historical'
    }
} finally {
    Assert-TestRoot
    $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        $resolvedRoot -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) { throw 'Unsafe test cleanup target' }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
Write-Host "Production boundary: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
