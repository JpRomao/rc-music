#Requires -Version 7.0
[CmdletBinding()]
param([switch] $IncludeLegacy)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $PSScriptRoot '../RcMusicRuntimePlan.psm1') -Force
$testParent = Join-Path $repository 'out/product/test-runs/rc-music-runtime-plan'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId

function Assert-TestAncestors([string] $Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while (-not [string]::IsNullOrEmpty($current)) {
        try {
            if (([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Unsafe test path: reparse point'
            }
        } catch {
            $cause = $_.Exception.GetBaseException()
            if ($cause -isnot [IO.FileNotFoundException] -and $cause -isnot [IO.DirectoryNotFoundException]) { throw }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

Assert-TestAncestors $testRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test GUID directory must be fresh' }
$null = New-Item -ItemType Directory -Path $testRoot
$emptyGitConfiguration = Join-Path $testRoot 'empty-git-configuration'
$null = New-Item -ItemType Directory -Path $emptyGitConfiguration
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
    foreach ($name in @('GIT_DIR', 'GIT_WORK_TREE', 'GIT_COMMON_DIR', 'GIT_INDEX_FILE',
            'GIT_OBJECT_DIRECTORY', 'GIT_ALTERNATE_OBJECT_DIRECTORIES')) {
        if (-not [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($name))) {
            throw 'Fixture Git cannot inherit an external repository override'
        }
    }
    $result = & git -c "core.hooksPath=$emptyGitConfiguration" -c "init.templateDir=$emptyGitConfiguration" `
        -c commit.gpgsign=false -c tag.gpgsign=false -C $Directory @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ('Fixture git failed: ' + ($result -join ' ')) }
    return $result
}
function New-SourceRepository([switch] $WithProductSources, [switch] $ProductOnly) {
    $s = New-Scenario
    $directory = Join-Path $s.Base 'source-repository'
    $inputPaths = @('experiments/m0/candidates/qt/shell', 'experiments/m0/protocol', 'experiments/m0/shared/shell-fixture')
    $productInputPaths = @('CMakeLists.txt', 'cmake', 'include', 'src', 'tests', 'tools', 'assets')
    foreach ($path in $(if ($ProductOnly) { @() } else { $inputPaths })) {
        $parent = Join-Path $directory $path
        $null = New-Item -ItemType Directory -Path $parent
        [IO.File]::WriteAllText((Join-Path $parent 'input.txt'), 'committed synthetic source')
    }
    if ($WithProductSources -or $ProductOnly) {
        foreach ($file in @('CMakeLists.txt', 'cmake/Core.cmake', 'include/core.hpp', 'src/core.cpp', 'tests/core.cpp',
                'tools/icon.cpp', 'assets/icon.svg')) {
            $target = Join-Path $directory $file
            $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force
            [IO.File]::WriteAllText($target, 'committed synthetic product source')
        }
    }
    if ($ProductOnly) {
        $packaging = Join-Path $directory 'packaging/windows'
        $null = New-Item -ItemType Directory -Path $packaging
        [IO.File]::WriteAllText((Join-Path $packaging 'RcMusicRuntimePlan.psm1'), '# synthetic product recipe')
    }
    [IO.File]::WriteAllText((Join-Path $directory 'README.md'), 'committed synthetic docs')
    Invoke-FixtureGit $directory @('init', "--template=$emptyGitConfiguration", '--initial-branch=main') | Out-Null
    $trackedPaths = @(if (-not $ProductOnly) { $inputPaths })
    if ($WithProductSources -or $ProductOnly) { $trackedPaths += $productInputPaths }
    if ($ProductOnly) { $trackedPaths += 'packaging/windows' }
    Invoke-FixtureGit $directory (@('add', '--', 'README.md') + $trackedPaths) | Out-Null
    Invoke-FixtureGit $directory @('-c', 'user.name=RC Music Tests', '-c', 'user.email=rc-music-tests@example.invalid',
        '-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', 'Create synthetic source fixture') | Out-Null
    return [pscustomobject]@{
        Directory = $directory
        Commit = [string](Invoke-FixtureGit $directory @('rev-parse', 'HEAD'))
        InputPaths = $inputPaths + $productInputPaths + 'packaging/windows'
        ProductInputPaths = $productInputPaths + 'packaging/windows'
    }
}

try {
    if ($IncludeLegacy) {
        $legacyRoot = 'experiments/m0/candidates/qt/windows-installer/'
        foreach ($forwarder in @(
                @{ Entry = 'tests/WindowsRuntimePlan.Tests.ps1'; Target = 'tests/WindowsRuntimePlan.Tests.ps1'; Arguments = @() },
                @{ Entry = 'New-RcMusicLocalRuntime.ps1'; Target = 'New-RcMusicLocalRuntime.ps1'; Arguments = @('-OutputDirectory', 'unused-output') },
                @{ Entry = 'tests/LocalRuntime.Tests.ps1'; Target = 'tests/LocalRuntime.Tests.ps1'; Arguments = @() },
                @{ Entry = 'tests/LocalRuntime.Smoke.ps1'; Target = 'tests/LocalRuntime.Smoke.ps1'; Arguments = @('-RuntimeDirectory', 'unused-runtime') })) {
            Test-Case "historical forwarder propagates promoted setup failure: $($forwarder.Entry)" {
                $s = New-Scenario
                $export = Join-Path $s.Base 'forwarder-only export 日本'
                $entry = Join-Path $export ($legacyRoot + $forwarder.Entry)
                $target = Join-Path $export ('packaging/windows/' + $forwarder.Target)
                $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($entry)), ([IO.Path]::GetDirectoryName($target))
                Copy-Item -LiteralPath (Join-Path $repository ($legacyRoot + $forwarder.Entry)) -Destination $entry
                [IO.File]::WriteAllText($target, "throw 'SYNTHETIC_PROMOTED_SETUP_FAILURE'", [Text.UTF8Encoding]::new($false))
                $start = [Diagnostics.ProcessStartInfo]::new([Environment]::ProcessPath)
                $start.UseShellExecute = $false
                $start.CreateNoWindow = $true
                $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
                $start.WorkingDirectory = $emptyGitConfiguration
                $start.RedirectStandardOutput = $true
                $start.RedirectStandardError = $true
                foreach ($argument in (@('-NoProfile', '-NonInteractive', '-File', $entry) + $forwarder.Arguments)) {
                    $start.ArgumentList.Add($argument)
                }
                $process = [Diagnostics.Process]::new()
                $process.StartInfo = $start
                try {
                    if (-not $process.Start()) { throw 'Could not start isolated forwarder probe' }
                    $output = $process.StandardOutput.ReadToEndAsync()
                    $errors = $process.StandardError.ReadToEndAsync()
                    if (-not $process.WaitForExit(30000)) {
                        $process.Kill($true)
                        $process.WaitForExit()
                        throw 'Isolated forwarder probe timed out'
                    }
                    $stdout = $output.GetAwaiter().GetResult()
                    $stderr = $errors.GetAwaiter().GetResult()
                    if ($stderr -cnotmatch 'SYNTHETIC_PROMOTED_SETUP_FAILURE') { throw "Promoted stub did not run: $stdout $stderr" }
                    if ($process.ExitCode -eq 0) { throw 'Forwarder returned success after promoted setup failure' }
                } finally { $process.Dispose() }
            }
        }
    }
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
    Test-Case 'product source snapshot works without experiments and records only product inputs' {
        $source = New-SourceRepository -ProductOnly
        Assert-Equal (Test-Path -LiteralPath (Join-Path $source.Directory 'experiments')) $false
        $snapshot = Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory -EntryPoint product
        Assert-Equal $snapshot.commit $source.Commit
        Assert-Equal ($snapshot.input_paths -join '|') ($source.ProductInputPaths -join '|')
        Assert-Equal (Test-Path -LiteralPath (Join-Path $source.Directory 'experiments')) $false
    }
    foreach ($entrypoint in @('product', 'legacy')) {
        foreach ($change in @('unstaged', 'staged', 'untracked')) {
            Test-Case "source snapshot rejects $change packaging input in $entrypoint mode" {
                $source = New-SourceRepository -ProductOnly
                $relative = if ($change -ceq 'untracked') { 'packaging/windows/new.psm1' }
                    else { 'packaging/windows/RcMusicRuntimePlan.psm1' }
                [IO.File]::WriteAllText((Join-Path $source.Directory $relative), '# changed package source')
                if ($change -ceq 'staged') {
                    Invoke-FixtureGit $source.Directory @('add', '--', $relative) | Out-Null
                }
                Assert-Rejected { Get-RcMusicSourceSnapshot -RepositoryRoot $source.Directory -EntryPoint $entrypoint } `
                    'source inputs must be clean'
            }
        }
    }
    Test-Case 'exported product module exposes all APIs and copies bytes without experiments or caller CWD' {
        $s = New-Scenario
        $export = Join-Path $s.Base 'exported product 日本'
        $moduleDirectory = Join-Path $export 'packaging/windows'
        $null = New-Item -ItemType Directory -Path $moduleDirectory
        $module = Join-Path $moduleDirectory 'RcMusicRuntimePlan.psm1'
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../RcMusicRuntimePlan.psm1') -Destination $module
        $probe = Join-Path $export 'probe.ps1'
        [IO.File]::WriteAllText($probe, @'
param([string] $Module, [string] $Root)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath (Join-Path $Root 'experiments')) { throw 'Unexpected historical directory' }
$loaded = Import-Module -Name $Module -PassThru -Force
$expected = @('Copy-RcMusicRuntimePlan', 'Get-RcMusicDesktopBuildConfiguration', 'Get-RcMusicSourceSnapshot', 'New-RcMusicRuntimePlan')
if ((@($loaded.ExportedFunctions.Keys | Sort-Object) -join '|') -cne ($expected -join '|')) { throw 'Incorrect exported API' }
if ([IO.Path]::GetFullPath($loaded.Path) -cne [IO.Path]::GetFullPath($Module)) { throw 'Imported a module outside the export' }
$source = Join-Path $Root 'source.bin'
[IO.File]::WriteAllText($source, 'isolated synthetic bytes')
$plan = New-RcMusicRuntimePlan -Files @(@{ source = $source; relative_path = 'nested/RCMusic.exe' }) `
    -AllowedSourceRoots @($Root) -Destination (Join-Path $Root 'payload')
$null = Copy-RcMusicRuntimePlan -Plan $plan
if ([IO.File]::ReadAllText((Join-Path $Root 'payload/nested/RCMusic.exe')) -cne 'isolated synthetic bytes') { throw 'Wrong copied bytes' }
$cache = @{ CMAKE_HOME_DIRECTORY = $Root; CMAKE_BUILD_TYPE = 'Release'; BUILD_TESTING = 'OFF'; RC_MUSIC_BUILD_DESKTOP = 'ON' }
$configuration = Get-RcMusicDesktopBuildConfiguration -Cache $cache -RepositoryRoot $Root
if ($configuration.entrypoint -cne 'product') { throw 'Product configuration was not recognized' }
Write-Output 'ISOLATED_PRODUCT_PLANNER_OK'
'@, [Text.UTF8Encoding]::new($false))
        Assert-Equal (Test-Path -LiteralPath (Join-Path $export 'experiments')) $false
        $start = [Diagnostics.ProcessStartInfo]::new([Environment]::ProcessPath)
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $start.WorkingDirectory = $emptyGitConfiguration
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        foreach ($argument in @('-NoProfile', '-NonInteractive', '-File', $probe, '-Module', $module, '-Root', $export)) {
            $start.ArgumentList.Add($argument)
        }
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $start
        try {
            if (-not $process.Start()) { throw 'Could not start isolated PowerShell probe' }
            $output = $process.StandardOutput.ReadToEndAsync()
            $errors = $process.StandardError.ReadToEndAsync()
            if (-not $process.WaitForExit(30000)) {
                $process.Kill($true)
                $process.WaitForExit()
                throw 'Isolated PowerShell probe timed out'
            }
            $stdout = $output.GetAwaiter().GetResult()
            $stderr = $errors.GetAwaiter().GetResult()
            if ($process.ExitCode -ne 0) { throw "Isolated PowerShell probe failed: $stdout $stderr" }
            Assert-Equal $stderr ''
            Assert-Equal $stdout.Trim() 'ISOLATED_PRODUCT_PLANNER_OK'
        } finally { $process.Dispose() }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $export 'experiments')) $false
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
                if ($testing -ceq 'OFF') {
                    Assert-Equal $configuration.instrumented_executable $null
                    Assert-Equal $configuration.audio_integration_executable $null
                } elseif ($entrypoint -ceq 'product') {
                    Assert-Equal $configuration.instrumented_executable 'tests/process/rc_music_test_shell.exe'
                    Assert-Equal $configuration.audio_integration_executable 'rc_music_local_audio_integration_tests.exe'
                } else {
                    Assert-Equal $configuration.instrumented_executable 'm0_qt_shell.exe'
                    Assert-Equal $configuration.audio_integration_executable 'm0_qt_local_audio_integration_tests.exe'
                }
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
    Assert-TestAncestors $testRoot
    foreach ($junction in $junctions) {
        if (-not [IO.Path]::GetFullPath($junction).StartsWith($testRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture junction cleanup path' }
        Assert-TestAncestors ([IO.Path]::GetDirectoryName($junction))
        if (Test-Path -LiteralPath $junction) { Remove-Item -LiteralPath $junction -Force }
    }
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        $resolvedTestRoot -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) { throw 'Unsafe test cleanup target' }
    Assert-TestAncestors $resolvedTestRoot
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
Write-Host "Windows runtime plan: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
