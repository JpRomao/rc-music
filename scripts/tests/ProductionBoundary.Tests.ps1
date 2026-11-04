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
if (($args -join '|') -ceq ('-C|' + $PSScriptRoot + '|-t|commands|m0_qt_shell.exe')) {
    Get-Content -LiteralPath (Join-Path $PSScriptRoot 'commands.txt')
    $global:LASTEXITCODE = 0
    return
}
if (($args -join '|') -ceq ('-C|' + $PSScriptRoot + '|-t|query|m0_qt_shell.exe')) {
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'query-called.txt'), 'exact executable dependency query')
    Get-Content -LiteralPath (Join-Path $PSScriptRoot 'query.txt')
    $global:LASTEXITCODE = [int](Get-Content -LiteralPath (Join-Path $PSScriptRoot 'query-exit.txt') -Raw)
    return
}
throw 'Unexpected graph command'
'@)
    [IO.File]::WriteAllLines((Join-Path $directory 'query.txt'), @('m0_qt_shell.exe:', '  input: CXX_EXECUTABLE_LINKER',
            '    | Qt6Core.lib', '  outputs:', '    all'))
    [IO.File]::WriteAllText((Join-Path $directory 'query-exit.txt'), '0')
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
function New-CompleteBuild {
    $directory = New-Build
    $sdk = Join-Path $directory 'owned sdk 日本'
    foreach ($relative in @('build/native/include/WebView2.h', 'build/native/include/WebView2EnvironmentOptions.h',
            'build/native/x64/WebView2LoaderStatic.lib', 'LICENSE.txt', 'NOTICE.txt')) {
        $path = Join-Path $sdk $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Synthetic SDK input owned by this fixture: ' + $relative)
    }
    $cache = @('RC_MUSIC_BUILD_EMBEDDED_BROWSER:BOOL=ON', 'RC_MUSIC_BUILD_BROWSER_INTEGRATION:BOOL=OFF',
        'RC_MUSIC_BUILD_DOWNLOADS:BOOL=OFF', 'RC_MUSIC_BUILD_DOWNLOAD_NETWORK:BOOL=OFF',
        'RC_MUSIC_BUILD_DOWNLOAD_PERSISTENCE:BOOL=OFF', 'RC_MUSIC_BUILD_NATIVE_MESSAGING:BOOL=OFF',
        "RC_MUSIC_WEBVIEW2_SDK_ROOT:PATH=$sdk")
    [IO.File]::AppendAllText((Join-Path $directory 'CMakeCache.txt'), ($cache -join "`n") + "`n")
    [IO.File]::WriteAllLines((Join-Path $directory 'commands.txt'), @(
        'cl.exe /c "C:/source/src/presentation/PlayerPresenter.cpp"',
        'cl.exe /c "C:/source/src/presentation/LocalQueueListModel.cpp"',
        'cl.exe /c "C:/source/src/adapters/QtMultimediaPlaybackBackend.cpp"',
        'cl.exe /c "C:/source/src/adapters/WindowsWebView2Browser.cpp"',
        'cl.exe /c "C:/source/src/adapters/WindowsWebView2Options.cpp"',
        'cl.exe /c "C:/source/src/presentation/BrowserMediaPanel.cpp"',
        'link.exe /out:m0_qt_shell.exe @CMakeFiles/m0_qt_shell.rsp'))
    $library = (Join-Path $sdk 'build/native/x64/WebView2LoaderStatic.lib').Replace('\', '/')
    [IO.File]::WriteAllLines((Join-Path $directory 'query.txt'), @('m0_qt_shell.exe:', '  input: CXX_EXECUTABLE_LINKER',
            ('    | ' + $library), '  outputs:', '    all'))
    return [pscustomobject]@{ Directory = $directory; Sdk = $sdk; Library = $library }
}
function Assert-Rejected([string] $Directory, [string] $Pattern, [string] $Edition = 'Lite') {
    try { & (Join-Path $repository 'scripts/Test-ProductionBoundary.ps1') -BuildDirectory $Directory -Edition $Edition | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected boundary rejection'
}

try {
    foreach ($source in @('WindowsWebView2Browser.cpp', 'WindowsWebView2Options.cpp', 'BrowserMediaPanel.cpp')) {
        Test-Case "Lite rejects browser sources even when the embedded-browser cache option is absent: $source" {
            Assert-Rejected (New-Build 'product' ('cl.exe /c "C:/source/src/' + $source + '"')) 'Lite|browser|WebView2'
        }
    }
    Test-Case 'Lite rejects a browser loader visible in the commands graph' {
        Assert-Rejected (New-Build 'product' 'link.exe /out:m0_qt_shell.exe C:/owned/WebView2LoaderStatic.lib') 'Lite|browser|WebView2'
    }
    Test-Case 'Lite rejects a browser loader hidden from commands in the link response file' {
        $build = New-Build
        [IO.File]::WriteAllLines((Join-Path $build 'query.txt'), @('m0_qt_shell.exe:', '  input: CXX_EXECUTABLE_LINKER',
                '    | C:/owned/WebView2LoaderStatic.lib', '  outputs:', '    all'))
        Assert-Rejected $build 'Lite|browser|WebView2'
    }
    Test-Case 'default Lite still rejects an explicitly embedded-browser ON cache' {
        $complete = New-CompleteBuild
        Assert-Rejected $complete.Directory 'EMBEDDED_BROWSER|Lite'
    }
    Test-Case 'explicit Complete verifies all browser sources and the exact SDK loader dependency despite a response file' {
        $complete = New-CompleteBuild
        $output = & (Join-Path $repository 'scripts/Test-ProductionBoundary.ps1') -BuildDirectory $complete.Directory -Edition CompleteExperimental
        if ($output -notmatch 'OK: production graph inspected') { throw 'Complete graph was not inspected' }
        if (-not (Test-Path -LiteralPath (Join-Path $complete.Directory 'query-called.txt'))) { throw 'Executable dependency query was not issued' }
    }
    foreach ($missing in @('WindowsWebView2Browser.cpp', 'WindowsWebView2Options.cpp', 'BrowserMediaPanel.cpp')) {
        Test-Case "Complete rejects a missing browser source: $missing" {
            $complete = New-CompleteBuild
            $path = Join-Path $complete.Directory 'commands.txt'
            $commands = @(Get-Content -LiteralPath $path | Where-Object { $_ -notlike ('*' + $missing + '*') })
            [IO.File]::WriteAllLines($path, $commands)
            Assert-Rejected $complete.Directory 'missing.*graph|browser.*missing' 'CompleteExperimental'
        }
    }
    foreach ($change in @('missing-loader', 'loader-only-output', 'different-sdk', 'failed-query', 'empty-query', 'wrong-target')) {
        Test-Case "Complete rejects an unproven static loader dependency: $change" {
            $complete = New-CompleteBuild
            $lines = @('m0_qt_shell.exe:', '  input: CXX_EXECUTABLE_LINKER', ('    | ' + $complete.Library), '  outputs:', '    all')
            switch ($change) {
                'missing-loader' { $lines = @('m0_qt_shell.exe:', '  input: CXX_EXECUTABLE_LINKER', '    | Qt6Core.lib', '  outputs:', '    all') }
                'loader-only-output' { $lines = @('m0_qt_shell.exe:', '  input: CXX_EXECUTABLE_LINKER', '    | Qt6Core.lib', '  outputs:', ('    ' + $complete.Library)) }
                'different-sdk' { $lines[2] = '    | ' + (Join-Path $complete.Directory 'other-sdk/WebView2LoaderStatic.lib').Replace('\', '/') }
                'failed-query' { [IO.File]::WriteAllText((Join-Path $complete.Directory 'query-exit.txt'), '1') }
                'empty-query' { $lines = @() }
                'wrong-target' { $lines[0] = 'another-player.exe:' }
            }
            [IO.File]::WriteAllLines((Join-Path $complete.Directory 'query.txt'), $lines)
            Assert-Rejected $complete.Directory 'dependency|loader|query|target' 'CompleteExperimental'
        }
    }
    Test-Case 'Complete preserves the historical-support exclusion for dependencies hidden in a response file' {
        $complete = New-CompleteBuild
        [IO.File]::WriteAllLines((Join-Path $complete.Directory 'query.txt'), @('m0_qt_shell.exe:',
                '  input: CXX_EXECUTABLE_LINKER', ('    | ' + $complete.Library), '    | rc_music_test_support.lib',
                '  outputs:', '    all'))
        Assert-Rejected $complete.Directory 'Historical' 'CompleteExperimental'
    }
    Test-Case 'Complete still rejects instrumented caches before graph inspection' {
        $complete = New-CompleteBuild
        $path = Join-Path $complete.Directory 'CMakeCache.txt'
        $cache = (Get-Content -LiteralPath $path -Raw).Replace('BUILD_TESTING:BOOL=OFF', 'BUILD_TESTING:BOOL=ON')
        [IO.File]::WriteAllText($path, $cache)
        Assert-Rejected $complete.Directory 'BUILD_TESTING|configuration|build' 'CompleteExperimental'
    }
    Test-Case 'Complete requires the owned plain SDK inputs instead of accepting a directory label' {
        $complete = New-CompleteBuild
        Remove-Item -LiteralPath (Join-Path $complete.Sdk 'build/native/x64/WebView2LoaderStatic.lib')
        Assert-Rejected $complete.Directory 'SDK|source|file|missing' 'CompleteExperimental'
    }
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
