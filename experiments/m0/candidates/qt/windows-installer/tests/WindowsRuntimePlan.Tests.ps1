#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../WindowsRuntimePlan.psm1') -Force

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$testParent = Join-Path $repository 'out/m0/test-runs/rc-music-runtime-plan'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0; Next = 0 }
$junctions = [Collections.Generic.List[string]]::new()

function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'" }
}
function Assert-Rejected([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected rejection'
}
function New-Scenario {
    $state.Next++
    $base = Join-Path $testRoot ('case-' + $state.Next)
    $qt = Join-Path $base 'Qt SDK'
    $build = Join-Path $base 'build 日本'
    $outside = Join-Path $base 'Qt SDK-other'
    $null = New-Item -ItemType Directory -Path (Join-Path $qt 'bin'), $build, $outside
    $library = Join-Path $qt 'bin/Qt6Core.dll'
    $executable = Join-Path $build 'm0_qt_shell.exe'
    [IO.File]::WriteAllText($library, 'synthetic Qt bytes')
    [IO.File]::WriteAllText($executable, 'synthetic app bytes')
    [IO.File]::WriteAllText((Join-Path $outside 'foreign.dll'), 'outside sentinel')
    return [pscustomobject]@{
        Base = $base; Qt = $qt; Build = $build; Outside = $outside
        Library = $library; Exe = $executable
        Destination = Join-Path $base 'payload RC Music ação'
        Files = @(
            [pscustomobject]@{ source = $library; relative_path = 'Qt6Core.dll' },
            [pscustomobject]@{ source = $executable; relative_path = 'RCMusic.exe' }
        )
    }
}
function New-Plan($Scenario) {
    New-RcMusicRuntimePlan -Files $Scenario.Files -AllowedSourceRoots @($Scenario.Qt, $Scenario.Build) `
        -Destination $Scenario.Destination
}
function New-TestJunction([string] $Path, [string] $Target) {
    foreach ($value in @($Path, $Target)) {
        if (-not [IO.Path]::GetFullPath($value).StartsWith($testRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture junction path' }
    }
    $null = New-Item -ItemType Junction -Path $Path -Target $Target
    $junctions.Add($Path)
}
function Invoke-FixtureGit([string] $Directory, [string[]] $Arguments) {
    $result = & git -C $Directory @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ('Fixture git failed: ' + ($result -join ' ')) }
    return $result
}
function New-SourceRepository([switch] $WithProductSources) {
    $s = New-Scenario
    $directory = Join-Path $s.Base 'source-repository'
    $inputPaths = @('experiments/m0/candidates/qt/shell', 'experiments/m0/protocol', 'experiments/m0/shared/shell-fixture')
    $productInputPaths = @('CMakeLists.txt', 'cmake', 'include', 'src', 'tests', 'tools', 'assets')
    foreach ($path in $inputPaths) {
        $parent = Join-Path $directory $path
        $null = New-Item -ItemType Directory -Path $parent
        [IO.File]::WriteAllText((Join-Path $parent 'input.txt'), 'committed synthetic source')
    }
    if ($WithProductSources) {
        foreach ($file in @('CMakeLists.txt', 'cmake/Core.cmake', 'include/core.hpp', 'src/core.cpp', 'tests/core.cpp',
                'tools/icon.cpp', 'assets/icon.svg')) {
            $target = Join-Path $directory $file
            $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force
            [IO.File]::WriteAllText($target, 'committed synthetic product source')
        }
    }
    [IO.File]::WriteAllText((Join-Path $directory 'README.md'), 'committed synthetic docs')
    Invoke-FixtureGit $directory @('init', '--initial-branch=main') | Out-Null
    $trackedPaths = $inputPaths
    if ($WithProductSources) { $trackedPaths += $productInputPaths }
    Invoke-FixtureGit $directory (@('add', '--', 'README.md') + $trackedPaths) | Out-Null
    Invoke-FixtureGit $directory @('-c', 'user.name=RC Music Tests', '-c', 'user.email=rc-music-tests@example.invalid',
        '-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', 'Create synthetic source fixture') | Out-Null
    return [pscustomobject]@{
        Directory = $directory
        Commit = [string](Invoke-FixtureGit $directory @('rev-parse', 'HEAD'))
        InputPaths = $inputPaths + $productInputPaths
    }
}

try {
    Test-Case 'plans explicit sources and renamed app without creating the destination' {
        $s = New-Scenario
        $plan = New-Plan $s
        Assert-Equal $plan.files.Count 2
        Assert-Equal $plan.destination $s.Destination
        Assert-Equal $plan.files[1].relative_path 'RCMusic.exe'
        Assert-Equal $plan.files[1].source $s.Exe
        Assert-Equal $plan.files[0].size_bytes (Get-Item -LiteralPath $s.Library).Length
        Assert-Equal $plan.files[0].sha256 (Get-FileHash -LiteralPath $s.Library).Hash.ToLowerInvariant()
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'copies only planned bytes into a fresh Unicode destination' {
        $s = New-Scenario
        $s.Files[0].relative_path = 'plugins\audio/Qt6Core.dll'
        $plan = New-Plan $s
        Copy-RcMusicRuntimePlan -Plan $plan | Out-Null
        Assert-Equal ([IO.File]::ReadAllText((Join-Path $s.Destination 'RCMusic.exe'))) 'synthetic app bytes'
        Assert-Equal ([IO.File]::ReadAllText((Join-Path $s.Destination 'plugins/audio/Qt6Core.dll'))) 'synthetic Qt bytes'
        Assert-Equal @(Get-ChildItem -LiteralPath $s.Destination -File -Recurse).Count 2
    }
    Test-Case 'rejects an empty or malformed mapping and an empty roots list' {
        $s = New-Scenario
        Assert-Rejected { New-RcMusicRuntimePlan -Files @() -AllowedSourceRoots @($s.Qt) -Destination $s.Destination } 'empty|files'
        Assert-Rejected { New-RcMusicRuntimePlan -Files @(@{ source = $s.Library }) -AllowedSourceRoots @($s.Qt) -Destination $s.Destination } 'relative|mapping'
        Assert-Rejected { New-RcMusicRuntimePlan -Files $s.Files -AllowedSourceRoots @() -Destination $s.Destination } 'root|empty'
    }
    Test-Case 'rejects existing sources outside the allowed roots including prefix siblings' {
        $s = New-Scenario
        $s.Files[0].source = Join-Path $s.Outside 'foreign.dll'
        Assert-Rejected { New-Plan $s } 'outside|source root'
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'rejects missing sources and directory sources' {
        $s = New-Scenario
        $s.Files[0].source = Join-Path $s.Qt 'missing.dll'
        Assert-Rejected { New-Plan $s } 'missing|exist|source'
        $s.Files[0].source = $s.Qt
        Assert-Rejected { New-Plan $s } 'file|directory|source'
    }
    Test-Case 'rejects relative and traversal-bearing source paths' {
        $s = New-Scenario
        $s.Files[0].source = 'Qt6Core.dll'
        Assert-Rejected { New-Plan $s } 'absolute|qualified|source'
        $s.Files[0].source = Join-Path $s.Qt 'bin/../bin/Qt6Core.dll'
        Assert-Rejected { New-Plan $s } 'traversal|segment|path'
    }
    Test-Case 'rejects escaping rooted and Windows-alias destination paths' {
        $s = New-Scenario
        foreach ($path in @('../escape.dll', 'plugins/../escape.dll', '/rooted.dll', 'C:\rooted.dll',
                'C:relative.dll', '\\server\share\file.dll', 'x:stream', 'plugins/./file.dll',
                'plugins//file.dll', 'CON.dll', 'trailing. ', ('nul' + [char]0 + '.dll'))) {
            $s.Files[0].relative_path = $path
            Assert-Rejected { New-Plan $s } 'relative|path|segment|traversal|name'
        }
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'rejects destinations duplicated across case and separator variants' {
        $s = New-Scenario
        $s.Files[0].relative_path = 'plugins/library.dll'
        $s.Files[1].relative_path = 'PLUGINS\LIBRARY.DLL'
        Assert-Rejected { New-Plan $s } 'duplicate|collision'
    }
    Test-Case 'rejects reserved Windows device names with superscript digits before copying' {
        $s = New-Scenario
        foreach ($path in @('COM¹.dll', 'com²', 'plugins/COM³.dat', 'LPT¹.dll', 'lpt²', 'plugins/LPT³')) {
            $s.Files[0].relative_path = $path
            Assert-Rejected { New-Plan $s } 'name|path'
        }
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'rejects a planned file that would also have to be a directory' {
        $s = New-Scenario
        $s.Files[0].relative_path = 'plugins'
        $s.Files[1].relative_path = 'PLUGINS/app.dll'
        Assert-Rejected { New-Plan $s } 'collision|directory'
    }
    Test-Case 'rejects an existing destination without touching its content' {
        $s = New-Scenario
        $null = New-Item -ItemType Directory -Path $s.Destination
        $sentinel = Join-Path $s.Destination 'sentinel.txt'
        [IO.File]::WriteAllText($sentinel, 'keep me')
        Assert-Rejected { New-Plan $s } 'destination|exist'
        Assert-Equal ([IO.File]::ReadAllText($sentinel)) 'keep me'
    }
    Test-Case 'rejects a source below a junction even when lexically contained' {
        $s = New-Scenario
        $link = Join-Path $s.Qt 'linked'
        New-TestJunction $link $s.Outside
        $s.Files[0].source = Join-Path $link 'foreign.dll'
        Assert-Rejected { New-Plan $s } 'reparse|junction|link'
    }
    Test-Case 'rejects an allowed root that is itself a junction' {
        $s = New-Scenario
        $link = Join-Path $s.Base 'linked-root'
        New-TestJunction $link $s.Qt
        $s.Files[0].source = Join-Path $link 'bin/Qt6Core.dll'
        Assert-Rejected { New-RcMusicRuntimePlan -Files $s.Files -AllowedSourceRoots @($link, $s.Build) -Destination $s.Destination } 'reparse|junction|link'
    }
    Test-Case 'rejects a destination below a junction without touching its target' {
        $s = New-Scenario
        $link = Join-Path $s.Base 'linked-destination'
        New-TestJunction $link $s.Outside
        $s.Destination = Join-Path $link 'payload'
        Assert-Rejected { New-Plan $s } 'reparse|junction|link'
        Assert-Equal (Test-Path -LiteralPath (Join-Path $s.Outside 'payload')) $false
    }
    Test-Case 'revalidates every source hash before creating the destination' {
        $s = New-Scenario
        $plan = New-Plan $s
        [IO.File]::WriteAllText($s.Exe, 'changed app bytes')
        Assert-Rejected { Copy-RcMusicRuntimePlan -Plan $plan } 'changed|hash|size'
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'revalidates plan paths before copying' {
        $s = New-Scenario
        $plan = New-Plan $s
        Assert-Equal $plan.files.Count 2
        $plan.files[0].relative_path = '../escape.dll'
        Assert-Rejected { Copy-RcMusicRuntimePlan -Plan $plan } 'relative|path|traversal'
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'rejects a destination created after planning instead of overwriting it' {
        $s = New-Scenario
        $plan = New-Plan $s
        $null = New-Item -ItemType Directory -Path $s.Destination
        $sentinel = Join-Path $s.Destination 'RCMusic.exe'
        [IO.File]::WriteAllText($sentinel, 'keep newer file')
        Assert-Rejected { Copy-RcMusicRuntimePlan -Plan $plan } 'destination|exist'
        Assert-Equal ([IO.File]::ReadAllText($sentinel)) 'keep newer file'
    }
    Test-Case 'revalidates destination ancestors before copying' {
        $s = New-Scenario
        $link = Join-Path $s.Base 'later-parent'
        $s.Destination = Join-Path $link 'payload'
        $plan = New-Plan $s
        New-TestJunction $link $s.Outside
        Assert-Rejected { Copy-RcMusicRuntimePlan -Plan $plan } 'reparse|junction|link'
        Assert-Equal (Test-Path -LiteralPath (Join-Path $s.Outside 'payload')) $false
    }
    Test-Case 'source snapshot covers build inputs while ignoring docs and runtime-recipe dirt' {
        $source = New-SourceRepository
        [IO.File]::WriteAllText((Join-Path $source.Directory 'README.md'), 'pending documentation')
        $recipes = Join-Path $source.Directory 'experiments/m0/candidates/qt/windows-installer'
        $null = New-Item -ItemType Directory -Path $recipes
        [IO.File]::WriteAllText((Join-Path $recipes 'local-recipe.ps1'), '# untracked synthetic recipe')
        $snapshot = Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory
        Assert-Equal $snapshot.commit $source.Commit
        Assert-Equal ($snapshot.input_paths -join '|') ($source.InputPaths -join '|')
    }
    Test-Case 'source snapshot rejects an unstaged protocol change' {
        $source = New-SourceRepository
        [IO.File]::WriteAllText((Join-Path $source.Directory 'experiments/m0/protocol/input.txt'), 'changed protocol')
        Assert-Rejected { Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory } 'dirty|changed|clean|input'
    }
    Test-Case 'source snapshot rejects an untracked shell fixture input' {
        $source = New-SourceRepository
        [IO.File]::WriteAllText((Join-Path $source.Directory 'experiments/m0/shared/shell-fixture/new.txt'), 'new shared fixture')
        Assert-Rejected { Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory } 'dirty|changed|clean|input'
    }
    Test-Case 'source snapshot rejects a staged shell input change' {
        $source = New-SourceRepository
        $path = 'experiments/m0/candidates/qt/shell/input.txt'
        [IO.File]::WriteAllText((Join-Path $source.Directory $path), 'staged shell change')
        Invoke-FixtureGit $source.Directory @('add', '--', $path) | Out-Null
        Assert-Rejected { Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory } 'dirty|changed|clean|input'
    }
    Test-Case 'source snapshot records the clean HEAD and all promoted product input paths' {
        $source = New-SourceRepository -WithProductSources
        $snapshot = Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory
        Assert-Equal $snapshot.commit $source.Commit
        Assert-Equal ($snapshot.input_paths -join '|') ($source.InputPaths -join '|')
    }
    foreach ($path in @('CMakeLists.txt', 'cmake/Core.cmake', 'include/core.hpp', 'src/core.cpp', 'tests/core.cpp',
            'tools/icon.cpp', 'assets/icon.svg')) {
        Test-Case "source snapshot rejects an unstaged promoted input: $path" {
            $source = New-SourceRepository -WithProductSources
            [IO.File]::WriteAllText((Join-Path $source.Directory $path), 'changed product source')
            Assert-Rejected { Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory } 'dirty|changed|clean|input'
        }
        Test-Case "source snapshot rejects a staged promoted input: $path" {
            $source = New-SourceRepository -WithProductSources
            [IO.File]::WriteAllText((Join-Path $source.Directory $path), 'staged product source')
            Invoke-FixtureGit $source.Directory @('add', '--', $path) | Out-Null
            Assert-Rejected { Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory } 'dirty|changed|clean|input'
        }
        Test-Case "source snapshot rejects an untracked promoted input: $path" {
            $source = New-SourceRepository
            $target = Join-Path $source.Directory $path
            $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force
            [IO.File]::WriteAllText($target, 'untracked product source')
            Assert-Rejected { Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory } 'dirty|changed|clean|input'
        }
    }
    foreach ($entrypoint in @('product', 'legacy')) {
        foreach ($testing in @('ON', 'OFF')) {
            Test-Case "maps the $entrypoint desktop entrypoint with BUILD_TESTING=$testing" {
                $source = New-SourceRepository
                $cache = @{ CMAKE_BUILD_TYPE = 'Release'; BUILD_TESTING = $testing }
                if ($entrypoint -ceq 'product') {
                    $cache.CMAKE_HOME_DIRECTORY = $source.Directory
                    $cache.RC_MUSIC_BUILD_DESKTOP = 'ON'
                } else {
                    $cache.CMAKE_HOME_DIRECTORY = Join-Path $source.Directory 'experiments/m0/candidates/qt/shell'
                    $cache.M0_QT_SHELL_SUPPORT = 'ON'
                    $cache.M0_QT_AUDIO_SUPPORT = 'ON'
                }
                $configuration = Get-RcMusicDesktopBuildConfiguration -Cache $cache `
                    -RepositoryRoot $source.Directory -BuildTesting $testing
                Assert-Equal $configuration.entrypoint $entrypoint
                Assert-Equal $configuration.target 'm0_qt_shell'
                Assert-Equal $configuration.executable 'm0_qt_shell.exe'
                Assert-Equal $configuration.qml_source (Join-Path $source.Directory 'src/ui/qml')
            }
        }
    }
    foreach ($invalid in @('unknown entrypoint', 'missing product flag', 'disabled product flag', 'debug',
            'wrong testing mode', 'legacy missing audio', 'legacy missing shell', 'relative home')) {
        Test-Case "rejects unsupported desktop build: $invalid" {
            $source = New-SourceRepository
            $cache = @{ CMAKE_HOME_DIRECTORY = $source.Directory; CMAKE_BUILD_TYPE = 'Release'
                BUILD_TESTING = 'OFF'; RC_MUSIC_BUILD_DESKTOP = 'ON' }
            switch ($invalid) {
                'unknown entrypoint' { $cache.CMAKE_HOME_DIRECTORY = Join-Path $source.Directory 'foreign' }
                'missing product flag' { $cache.Remove('RC_MUSIC_BUILD_DESKTOP') }
                'disabled product flag' { $cache.RC_MUSIC_BUILD_DESKTOP = 'OFF' }
                'debug' { $cache.CMAKE_BUILD_TYPE = 'Debug' }
                'wrong testing mode' { $cache.BUILD_TESTING = 'ON' }
                'legacy missing audio' {
                    $cache.CMAKE_HOME_DIRECTORY = Join-Path $source.Directory 'experiments/m0/candidates/qt/shell'
                    $cache.M0_QT_SHELL_SUPPORT = 'ON'
                }
                'legacy missing shell' {
                    $cache.CMAKE_HOME_DIRECTORY = Join-Path $source.Directory 'experiments/m0/candidates/qt/shell'
                    $cache.M0_QT_AUDIO_SUPPORT = 'ON'
                }
                'relative home' { $cache.CMAKE_HOME_DIRECTORY = 'relative' }
            }
            Assert-Rejected { Get-RcMusicDesktopBuildConfiguration -Cache $cache `
                -RepositoryRoot $source.Directory -BuildTesting 'OFF' } '^Unsupported desktop build'
        }
    }
} finally {
    foreach ($junction in $junctions) {
        if (Test-Path -LiteralPath $junction) { Remove-Item -LiteralPath $junction -Force }
    }
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        $resolvedTestRoot -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) { throw 'Unsafe test cleanup target' }
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
Write-Host "Windows runtime plan: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
