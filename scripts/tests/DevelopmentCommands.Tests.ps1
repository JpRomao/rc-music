#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module = Join-Path $repository 'scripts/DevelopmentCommands.psm1'
if (Test-Path -LiteralPath $module) { Import-Module $module -Force }
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Rejected([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected rejection'
}
$tools = @{
    CMake = 'C:/tools/cmake.exe'; CTest = 'C:/tools/ctest.exe'; Ninja = 'C:/tools/ninja.exe'
    Compiler = 'C:/tools/cl.exe'; ClangFormat = 'C:/tools/clang-format.exe'
    QmlFormat = 'C:/tools/qmlformat.exe'; QtPrefix = 'C:/SDK 日本 & tools/qt'
    QtMultimediaPackage = 'C:/SDK/Multimedia'; QtHost = 'C:/SDK/host'
    TagLibPrefix = 'C:/SDK/taglib'; Catch2Source = 'C:/SDK/catch2'
    VisualStudioRoot = 'C:/tools/VS'
}
$build = Join-Path $repository 'out/product/runner-fixture'
function Plan([string] $Task, [switch] $Apply) {
    @(Get-RcMusicDevelopmentPlan -Task $Task -RepositoryRoot $repository -BuildDirectory $build -Tools $tools -Apply:$Apply)
}
Test-Case 'build configures root Release production then builds only the application' {
    $plan = Plan build
    Assert-True ($plan.Count -eq 2) 'Unexpected production steps'
    Assert-True ($plan[0].Arguments -contains $repository) 'Missing root source'
    Assert-True ($plan[0].Arguments -contains '-DCMAKE_BUILD_TYPE=Release') 'Missing Release'
    Assert-True ($plan[0].Arguments -contains '-DBUILD_TESTING=OFF') 'Production must exclude tests'
    Assert-True ($plan[1].Arguments -contains 'm0_qt_shell') 'Missing application target'
}
Test-Case 'test configures ON and builds before running all tests with empty suites rejected' {
    $plan = Plan test
    Assert-True (($plan.Name -join '|') -ceq 'configure|build|test') 'Wrong test order'
    Assert-True ($plan[0].Arguments -contains '-DBUILD_TESTING=ON') 'Missing tests ON'
    Assert-True ($plan[2].Arguments -contains '--no-tests=error') 'Empty suite could pass'
    Assert-True ($plan[2].Arguments -contains '--output-on-failure') 'Missing diagnostics'
    Assert-True (-not ($plan[2].Arguments -contains '-R')) 'Default test suite must not be filtered'
}
Test-Case 'validate includes docs formatter runner tests build lint and test once' {
    $plan = Plan validate
    Assert-True (($plan.Name -join '|') -ceq 'format|docs|traceability-tests|traceability|runner-tests|format-tests|diff|configure|build|lint|test') 'Incomplete or repeated validate'
    Assert-True (-not ($plan[0].Arguments -contains '-Apply')) 'Validate must not format in place'
}
Test-Case 'check does not compile or modify source files' {
    $plan = Plan check
    Assert-True (($plan.Name -join '|') -ceq 'format|docs|traceability-tests|traceability|runner-tests|format-tests|diff') 'Wrong check steps'
    Assert-True (-not ($plan.Arguments -contains '-Apply')) 'Check mutates sources'
}
Test-Case 'only explicit format apply permits source formatting' {
    $plan = Plan format -Apply
    Assert-True ($plan[0].Arguments -contains '-Apply') 'Explicit apply ignored'
    Assert-Rejected { Plan validate -Apply } 'Apply.*format'
}
Test-Case 'lint checks compiler diagnostics and QML on the same build' {
    $plan = Plan lint
    Assert-True (($plan.Name -join '|') -ceq 'configure|build|lint') 'Lint must build first'
    Assert-True ($plan[2].Arguments -contains 'all_qmllint') 'QML lint missing'
}
Test-Case 'toolchain paths with spaces Unicode and metacharacters remain single arguments' {
    $plan = Plan build
    Assert-True ($plan[0].Arguments -contains '-DCMAKE_PREFIX_PATH=C:/SDK 日本 & tools/qt;C:/SDK/taglib') 'Path was split or reinterpreted'
    Assert-True ($plan[0].File -ceq $tools.CMake) 'Wrong executable'
}
Test-Case 'development plans never invoke experiments installers benchmarks or downloads' {
    foreach ($task in @('build', 'test', 'lint', 'check', 'validate', 'format')) {
        $text = (Plan $task | ConvertTo-Json -Depth 5)
        Assert-True ($text -notmatch 'experiments[/\\]|benchmark|https://|install|cargo|pnpm') 'Out-of-scope dependency'
    }
}
Test-Case 'execution stops immediately on a failing command and propagates its status' {
    $calls = [Collections.Generic.List[string]]::new()
    $plan = Plan test
    $invoke = {
        param($step)
        $calls.Add($step.Name)
        [pscustomobject]@{ ExitCode = $(if ($step.Name -ceq 'build') { 7 } else { 0 }); Output = @() }
    }.GetNewClosure()
    Assert-Rejected { Invoke-RcMusicDevelopmentPlan -Plan $plan -Invoker $invoke } 'build.*7'
    Assert-True (($calls -join '|') -ceq 'configure|build') 'Execution continued after failure'
}
Test-Case 'execution rejects missing process results instead of treating them as success' {
    Assert-Rejected { Invoke-RcMusicDevelopmentPlan -Plan (Plan build) -Invoker { param($step) $null } } 'result'
}
Test-Case 'build directory must stay in the product output area and outside its root' {
    Assert-Rejected { Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory $repository -Testing OFF } 'output'
    Assert-Rejected { Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory (Join-Path $repository 'out/product') -Testing OFF } 'output'
}
$testRoot = Join-Path $repository ('out/product/runner-contract-' + [guid]::NewGuid().ToString('N'))
function Assert-SafeTestRoot {
    $parent = [IO.Path]::GetFullPath((Join-Path $repository 'out/product')).TrimEnd('\') + '\'
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^runner-contract-[0-9a-f]{32}$') { throw 'Unsafe test directory' }
    $ancestor = $resolved
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Test directory may not traverse a reparse point'
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
Assert-SafeTestRoot
$null = New-Item -ItemType Directory -Path $testRoot
try {
    Test-Case 'fresh product build directory is accepted without creating cache files' {
        $fresh = Join-Path $testRoot 'fresh'
        Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory $fresh -Testing ON
        Assert-True (-not (Test-Path -LiteralPath $fresh)) 'Validation created the build'
    }
    foreach ($invalid in @('foreign home', 'Debug', 'testing OFF', 'desktop OFF', 'duplicate cache', 'wrong generator')) {
        Test-Case "rejects a configured $invalid before any build" {
            $cache = @("CMAKE_HOME_DIRECTORY:INTERNAL=$repository", 'CMAKE_GENERATOR:INTERNAL=Ninja',
                'CMAKE_BUILD_TYPE:STRING=Release', 'BUILD_TESTING:BOOL=ON', 'RC_MUSIC_BUILD_DESKTOP:BOOL=ON')
            switch ($invalid) {
                'foreign home' { $cache[0] = 'CMAKE_HOME_DIRECTORY:INTERNAL=C:/foreign' }
                'Debug' { $cache[2] = 'CMAKE_BUILD_TYPE:STRING=Debug' }
                'testing OFF' { $cache[3] = 'BUILD_TESTING:BOOL=OFF' }
                'desktop OFF' { $cache[4] = 'RC_MUSIC_BUILD_DESKTOP:BOOL=OFF' }
                'duplicate cache' { $cache += 'BUILD_TESTING:BOOL=ON' }
                'wrong generator' { $cache[1] = 'CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022' }
            }
            [IO.File]::WriteAllLines((Join-Path $testRoot 'CMakeCache.txt'), $cache)
            Assert-Rejected { Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory $testRoot -Testing ON } 'cache|configuration'
        }
    }
    $sdkCache = @{
        CMAKE_HOME_DIRECTORY = $repository; CMAKE_GENERATOR = 'Ninja'; CMAKE_BUILD_TYPE = 'Release'
        BUILD_TESTING = 'ON'; RC_MUSIC_BUILD_DESKTOP = 'ON'; CMAKE_COMMAND = $tools.CMake
        CMAKE_CXX_COMPILER = $tools.Compiler; CMAKE_MAKE_PROGRAM = $tools.Ninja
        Qt6_DIR = "$($tools.QtPrefix)/lib/cmake/Qt6"; Qt6Core_DIR = "$($tools.QtPrefix)/lib/cmake/Qt6Core"
        Qt6LinguistTools_DIR = "$($tools.QtHost)/lib/cmake/Qt6LinguistTools"
        Qt6Multimedia_DIR = $tools.QtMultimediaPackage; QT_HOST_PATH = $tools.QtHost
        TagLib_DIR = "$($tools.TagLibPrefix)/lib/cmake/taglib"; RC_MUSIC_CATCH2_SOURCE_DIR = $tools.Catch2Source
    }
    foreach ($key in @('CMAKE_COMMAND', 'CMAKE_CXX_COMPILER', 'CMAKE_MAKE_PROGRAM', 'Qt6_DIR', 'Qt6Core_DIR',
            'Qt6LinguistTools_DIR', 'Qt6Multimedia_DIR', 'QT_HOST_PATH', 'TagLib_DIR', 'RC_MUSIC_CATCH2_SOURCE_DIR')) {
        Test-Case "cached $key must match the selected toolchain" {
            $cache = $sdkCache.Clone()
            $cache[$key] = 'C:/unrelated-sdk'
            [IO.File]::WriteAllLines((Join-Path $testRoot 'CMakeCache.txt'), @($cache.GetEnumerator() | ForEach-Object { "$($_.Key):PATH=$($_.Value)" }))
            Assert-Rejected { Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory $testRoot -Testing ON -Tools $tools } 'cached toolchain'
        }
    }
    Test-Case 'matching SDK cache accepts normalized Windows paths and selected host tools' {
        [IO.File]::WriteAllLines((Join-Path $testRoot 'CMakeCache.txt'), @($sdkCache.GetEnumerator() | ForEach-Object { "$($_.Key):PATH=$($_.Value.Replace('/', '\'))" }))
        Assert-RcMusicBuildDirectory -RepositoryRoot $repository -BuildDirectory $testRoot -Testing ON -Tools $tools
    }
    Test-Case 'real process failure propagates and restores the working directory' {
        $previous = (Get-Location).Path
        $step = [pscustomobject]@{ Name = 'native-failure'; File = (Get-Process -Id $PID).Path
            Arguments = @('-NoProfile', '-Command', 'exit 23'); WorkingDirectory = $testRoot }
        Assert-Rejected { Invoke-RcMusicDevelopmentPlan -Plan @($step) } 'native-failure.*23'
        Assert-True ((Get-Location).Path -eq $previous) 'Working directory was not restored'
    }
    Test-Case 'tool data paths resolve from repository and check requires only formatters' {
        $config = Join-Path $testRoot 'toolchain.psd1'
        [IO.File]::WriteAllText($config, "@{ ClangFormat = 'scripts/develop.ps1'; QmlFormat = 'scripts/Format-Source.ps1' }")
        $read = Read-RcMusicDevelopmentTools -Path $config -RepositoryRoot $repository -Task check
        Assert-True ($read.Count -eq 2) 'Check unexpectedly required the build toolchain'
        Assert-True ($read.ClangFormat -eq (Join-Path $repository 'scripts/develop.ps1')) 'Relative path used config directory'
    }
    Test-Case 'tool data rejects unknown fields missing tools and CMake list injection' {
        $config = Join-Path $testRoot 'invalid-tools.psd1'
        foreach ($content in @("@{ Unknown = 'x' }", '@{}', "@{ ClangFormat = 'C:/one;C:/two' }", "@{ ClangFormat = 42 }")) {
            [IO.File]::WriteAllText($config, $content)
            Assert-Rejected { Read-RcMusicDevelopmentTools -Path $config -RepositoryRoot $repository -Task check } 'field|path'
        }
    }
    Test-Case 'tool data requires existing paths of the right kind' {
        $config = Join-Path $testRoot 'missing-tools.psd1'
        foreach ($path in @('scripts', 'does-not-exist.exe')) {
            [IO.File]::WriteAllText($config, "@{ ClangFormat = '$path'; QmlFormat = 'scripts/Format-Source.ps1' }")
            Assert-Rejected { Read-RcMusicDevelopmentTools -Path $config -RepositoryRoot $repository -Task check } 'unavailable'
        }
    }
} finally {
    Assert-SafeTestRoot
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
Write-Host "Development commands: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
