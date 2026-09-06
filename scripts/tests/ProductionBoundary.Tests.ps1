#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testParent = Join-Path $repository 'out/m0/test-runs/production-boundary'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0; Next = 0 }

function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function New-Build([string] $Entrypoint = 'product', [string] $ExtraCommand = '') {
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
    $sourceHome = if ($Entrypoint -ceq 'product') { $repository }
        elseif ($Entrypoint -ceq 'legacy') { Join-Path $repository 'experiments/m0/candidates/qt/shell' }
        else { Join-Path $repository 'foreign-source' }
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
    $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        $resolvedRoot -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) { throw 'Unsafe test cleanup target' }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
Write-Host "Production boundary: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
