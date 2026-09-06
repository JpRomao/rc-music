#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module = Join-Path $repository 'scripts/DevelopmentDiagnostics.psm1'
if (Test-Path -LiteralPath $module) { Import-Module $module -Force }
Import-Module (Join-Path $repository 'scripts/DevelopmentCommands.psm1') -Force
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
$testRoot = Join-Path $repository ('out/product/diagnostics-contract-' + [guid]::NewGuid().ToString('N'))
function Assert-SafeTestRoot {
    $parent = [IO.Path]::GetFullPath((Join-Path $repository 'out/product')).TrimEnd('\', '/') + '\'
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^diagnostics-contract-[0-9a-f]{32}$') { throw 'Unsafe test root' }
    $ancestor = $resolved
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe reparse point' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Invoke-FixtureGit([string[]] $Arguments) {
    $output = @(& git -c "core.hooksPath=$emptyHooks" -c commit.gpgsign=false -c init.templateDir= -C $source @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git failed: $output" }
    return $output
}
function New-Run { New-RcMusicDevelopmentDiagnostics -RepositoryRoot $source -Task test -BuildDirectory (Join-Path $source 'out/product/tests') }
function Report($Run) { Get-Content -LiteralPath (Join-Path $Run.Directory 'run.json') -Raw | ConvertFrom-Json }
function Step([string] $Name, [string] $Command) {
    [pscustomobject]@{ Name = $Name; File = (Get-Process -Id $PID).Path
        Arguments = @('-NoProfile', '-Command', $Command); WorkingDirectory = $source }
}
Assert-SafeTestRoot
$null = New-Item -ItemType Directory -Path $testRoot
try {
    $source = Join-Path $testRoot 'source 日本 with spaces'
    $emptyHooks = Join-Path $testRoot 'empty-hooks'
    $null = New-Item -ItemType Directory -Path $emptyHooks
    $null = New-Item -ItemType Directory -Path $source
    $null = Invoke-FixtureGit @('init', '--quiet')
    [IO.File]::WriteAllText((Join-Path $source '.gitignore'), "/out/`n")
    $null = Invoke-FixtureGit @('add', '.gitignore')
    $null = Invoke-FixtureGit @('-c', 'user.name=Diagnostic Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '--quiet', '-m', 'fixture')
    $expectedCommit = [string](Invoke-FixtureGit @('rev-parse', 'HEAD'))
    Test-Case 'a run records real commit task selected build and clean state in its exclusive directory' {
        $run = New-Run
        Complete-RcMusicDevelopmentDiagnostics -Context $run
        $report = Report $run
        Assert-True ($run.Directory -match '[/\\]out[/\\]product[/\\]validation-runs[/\\]run-[0-9a-f]{32}$') 'Wrong diagnostic directory'
        Assert-True ($report.schemaVersion -eq 1 -and $report.commitStart -ceq $expectedCommit -and $report.commitEnd -ceq $expectedCommit) 'Commit not observed'
        Assert-True ($report.task -ceq 'test' -and $report.buildDirectory -ceq (Join-Path $source 'out/product/tests')) 'Selected action missing'
        Assert-True (-not $report.dirtyStart -and -not $report.dirtyEnd -and $report.status -ceq 'passed') 'Source state wrong'
        Assert-True ($report.startedUtc -and $report.finishedUtc) 'Run boundaries missing'
    }
    Test-Case 'success and failure get separate runs without replacing older evidence' {
        $first = New-Run
        Complete-RcMusicDevelopmentDiagnostics -Context $first
        $before = (Get-FileHash -LiteralPath (Join-Path $first.Directory 'run.json')).Hash
        $second = New-Run
        Complete-RcMusicDevelopmentDiagnostics -Context $second -Failed
        Assert-True ($first.Directory -cne $second.Directory -and (Report $second).status -ceq 'failed') 'Run reused or failure lost'
        Assert-True ((Get-FileHash -LiteralPath (Join-Path $first.Directory 'run.json')).Hash -ceq $before) 'Old evidence overwritten'
    }
    Test-Case 'native failure records stdout stderr exact exit and stops before the next step' {
        $run = New-Run
        $plan = @((Step 'first' '[Console]::Out.WriteLine("stdout 日本"); [Console]::Error.WriteLine("stderr Ω"); exit 23'), (Step 'later' 'exit 0'))
        Assert-Rejected { Invoke-RcMusicDevelopmentPlan -Plan $plan -DiagnosticsContext $run } 'first.*23'
        Complete-RcMusicDevelopmentDiagnostics -Context $run -Failed
        $report = Report $run
        Assert-True ($report.steps.Count -eq 1 -and $report.steps[0].exitCode -eq 23 -and $report.steps[0].status -ceq 'failed') 'Wrong failure or later execution'
        $log = Get-Content -LiteralPath (Join-Path $run.Directory $report.steps[0].log) -Raw
        Assert-True ($log.Contains('stdout 日本') -and $log.Contains('stderr Ω')) 'One output stream was lost'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $run.Directory 'ctest.xml'))) 'Stale JUnit invented'
    }
    Test-Case 'successful steps retain order zero exits and logs' {
        $run = New-Run
        Invoke-RcMusicDevelopmentPlan -Plan @((Step 'one' '"one"; exit 0'), (Step 'two' '"two"; exit 0')) -DiagnosticsContext $run
        Complete-RcMusicDevelopmentDiagnostics -Context $run
        $report = Report $run
        Assert-True (($report.steps.name -join '|') -ceq 'one|two' -and $report.status -ceq 'passed') 'Steps reordered'
        foreach ($step in $report.steps) {
            Assert-True ($step.exitCode -eq 0 -and $step.status -ceq 'passed' -and $step.finishedUtc) 'Step result absent'
            Assert-True (Test-Path -LiteralPath (Join-Path $run.Directory $step.log) -PathType Leaf) 'Step log absent'
        }
    }
    Test-Case 'launch failure is recorded without fabricating a process exit' {
        $run = New-Run
        $step = Step 'missing' 'exit 0'; $step.File = Join-Path $source 'not-present.exe'
        Assert-Rejected { Invoke-RcMusicDevelopmentPlan -Plan @($step) -DiagnosticsContext $run } 'not-present'
        Complete-RcMusicDevelopmentDiagnostics -Context $run -Failed
        $result = (Report $run).steps[0]
        Assert-True ($result.status -ceq 'failed' -and $null -eq $result.exitCode) 'Invented child exit'
    }
    foreach ($junitKind in @('missing', 'empty', 'present')) {
        Test-Case "successful CTest requires its own nonempty JUnit: $junitKind" {
            $run = New-Run
            $junit = Join-Path $run.Directory 'ctest.xml'
            $step = Step 'test' 'exit 0'; $step.Arguments += @('--output-junit', $junit)
            $invoke = {
                param($step)
                if ($junitKind -cne 'missing') {
                    [IO.File]::WriteAllText($junit, $(if ($junitKind -ceq 'present') { '<testsuite />' } else { '' }))
                }
                [pscustomobject]@{ ExitCode = 0; Output = @() }
            }.GetNewClosure()
            if ($junitKind -ceq 'present') {
                Invoke-RcMusicDevelopmentPlan -Plan @($step) -Invoker $invoke -DiagnosticsContext $run
                Complete-RcMusicDevelopmentDiagnostics -Context $run
                Assert-True ((Report $run).status -ceq 'passed') 'Delivered JUnit rejected'
            } else {
                Assert-Rejected { Invoke-RcMusicDevelopmentPlan -Plan @($step) -Invoker $invoke -DiagnosticsContext $run } 'JUnit'
                Complete-RcMusicDevelopmentDiagnostics -Context $run -Failed
            }
        }
    }
    Test-Case 'dirty changes during the run are visible but their paths and contents are not copied' {
        $run = New-Run
        $private = Join-Path $source 'private-config-sentinel.txt'
        [IO.File]::WriteAllText($private, 'DO-NOT-COPY-SENTINEL')
        try {
            Complete-RcMusicDevelopmentDiagnostics -Context $run
            $raw = Get-Content -LiteralPath (Join-Path $run.Directory 'run.json') -Raw
            $report = $raw | ConvertFrom-Json
            Assert-True (-not $report.dirtyStart -and $report.dirtyEnd) 'Dirty transition lost'
            Assert-True (-not $raw.Contains('DO-NOT-COPY-SENTINEL') -and -not $raw.Contains('private-config-sentinel')) 'Private source data copied'
        } finally { Remove-Item -LiteralPath $private }
    }
    Test-Case 'a reparse diagnostic parent is rejected before writing through its destination' {
        $other = Join-Path $testRoot 'junction-source'; $outside = Join-Path $testRoot 'junction-target'
        $null = New-Item -ItemType Directory -Path $other, $outside, (Join-Path $other 'out/product')
        $junction = Join-Path $other 'out/product/validation-runs'
        $null = New-Item -ItemType Junction -Path $junction -Target $outside
        try {
            Assert-Rejected { New-RcMusicDevelopmentDiagnostics -RepositoryRoot $other -Task check -BuildDirectory (Join-Path $other 'out/product/tests') } 'reparse'
            Assert-True (@(Get-ChildItem -LiteralPath $outside -Force).Count -eq 0) 'Wrote through junction'
        } finally { Assert-SafeTestRoot; Remove-Item -LiteralPath $junction -Force }
    }
    Test-Case 'a diagnostic parent occupied by a file is rejected without overwriting it' {
        $other = Join-Path $testRoot 'file-source'
        $null = New-Item -ItemType Directory -Path (Join-Path $other 'out/product')
        $occupied = Join-Path $other 'out/product/validation-runs'
        [IO.File]::WriteAllText($occupied, 'preserve')
        Assert-Rejected { New-RcMusicDevelopmentDiagnostics -RepositoryRoot $other -Task check -BuildDirectory (Join-Path $other 'out/product/tests') } 'directory'
        Assert-True ([IO.File]::ReadAllText($occupied) -ceq 'preserve') 'External file overwritten'
    }
    Test-Case 'diagnostic write failure never hides the native failure code' {
        $run = New-Run
        $held = @{ Stream = $null }
        $invoke = {
            param($step)
            $held.Stream = [IO.File]::Open((Join-Path $run.Directory 'run.json'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            [pscustomobject]@{ ExitCode = 23; Output = @('original failure') }
        }.GetNewClosure()
        $oldWarnings = $WarningPreference
        try {
            $WarningPreference = 'Stop'
            Assert-Rejected { Invoke-RcMusicDevelopmentPlan -Plan @((Step 'failed' 'exit 23')) -Invoker $invoke -DiagnosticsContext $run -WarningAction Stop } 'failed.*23'
        } finally { $WarningPreference = $oldWarnings; if ($held.Stream) { $held.Stream.Dispose() } }
        Complete-RcMusicDevelopmentDiagnostics -Context $run -Failed
        Assert-True ((Report $run).status -ceq 'failed') 'Diagnostic failure turned success'
    }
    Test-Case 'default CLI failure creates no diagnostic run while opt-in records preflight failure' {
        $fixture = Join-Path $source 'scripts'; $null = New-Item -ItemType Directory -Path $fixture
        foreach ($file in @('develop.ps1', 'DevelopmentCommands.psm1', 'DevelopmentDiagnostics.psm1')) {
            Copy-Item -LiteralPath (Join-Path $repository "scripts/$file") -Destination (Join-Path $fixture $file)
        }
        $runs = Join-Path $source 'out/product/validation-runs'
        $before = @((Get-ChildItem -LiteralPath $runs -Directory).Name)
        $pwsh = (Get-Process -Id $PID).Path
        $null = & $pwsh -NoProfile -File (Join-Path $fixture 'develop.ps1') check -ToolchainFile (Join-Path $source 'missing.psd1') 2>&1
        Assert-True ($LASTEXITCODE -ne 0) 'Default preflight failed open'
        Assert-True (((Get-ChildItem -LiteralPath $runs -Directory).Name -join '|') -ceq ($before -join '|')) 'Default created diagnostics'
        $output = @(& $pwsh -NoProfile -File (Join-Path $fixture 'develop.ps1') check -ToolchainFile (Join-Path $source 'missing.psd1') -Diagnostics 2>&1)
        Assert-True ($LASTEXITCODE -ne 0) 'Opt-in preflight failed open'
        $created = @(Get-ChildItem -LiteralPath $runs -Directory | Where-Object Name -NotIn $before)
        Assert-True ($created.Count -eq 1) "Preflight evidence absent: $output"
        $report = Get-Content -LiteralPath (Join-Path $created[0].FullName 'run.json') -Raw | ConvertFrom-Json
        Assert-True ($report.status -ceq 'failed' -and $report.steps.Count -eq 1 -and $report.steps[0].name -ceq 'preflight') 'Preflight failure not recorded'
        Assert-True ($null -eq $report.steps[0].exitCode -and $report.steps[0].status -ceq 'failed') 'Preflight fabricated a child result'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $created[0].FullName 'ctest.xml'))) 'Old test report copied'
    }
    Test-Case 'CLI records selected configuration fingerprint and resolved tools without copying data file contents' {
        $fixture = Join-Path $source 'scripts'
        Copy-Item -LiteralPath (Join-Path $repository 'scripts/Format-Source.ps1') -Destination (Join-Path $fixture 'Format-Source.ps1')
        $null = New-Item -ItemType Directory -Path (Join-Path $source 'include'), (Join-Path $source 'src/ui/qml')
        [IO.File]::WriteAllText((Join-Path $source 'include/example.cpp'), "int value;`n")
        [IO.File]::WriteAllText((Join-Path $source 'src/ui/qml/Example.qml'), "Item {} `n")
        $clang = Join-Path $source 'fake clang.ps1'; $qml = Join-Path $source 'fake qml.ps1'
        [IO.File]::WriteAllText($clang, 'if ($args -contains "--version") { "clang-format version 19.1.1" }; exit 0')
        [IO.File]::WriteAllText($qml, 'if ($args -contains "--version") { "qmlformat 6.11.2" } else { Get-Content -LiteralPath $args[-1] }; exit 0')
        $config = Join-Path $source 'selected tools.psd1'
        [IO.File]::WriteAllText($config, "# PRIVATE-CONFIG-CONTENT-NOT-COPIED`n@{ ClangFormat = 'fake clang.ps1'; QmlFormat = 'fake qml.ps1' }")
        $runs = Join-Path $source 'out/product/validation-runs'
        $before = @((Get-ChildItem -LiteralPath $runs -Directory).Name)
        $output = @(& (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $fixture 'develop.ps1') format -ToolchainFile $config -Diagnostics 2>&1)
        Assert-True ($LASTEXITCODE -eq 0) "Synthetic formatter invocation failed: $output"
        $created = @(Get-ChildItem -LiteralPath $runs -Directory | Where-Object Name -NotIn $before)
        Assert-True ($created.Count -eq 1) 'Configuration run absent'
        $raw = Get-Content -LiteralPath (Join-Path $created[0].FullName 'run.json') -Raw
        $report = $raw | ConvertFrom-Json
        Assert-True ($report.toolchainFile -ceq $config -and $report.toolchainSha256 -ceq (Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash.ToLowerInvariant()) 'Selected toolchain fingerprint absent'
        Assert-True ($report.selectedTools.ClangFormat -ceq $clang -and $report.selectedTools.QmlFormat -ceq $qml) 'Resolved tool selection absent'
        Assert-True (-not $raw.Contains('PRIVATE-CONFIG-CONTENT-NOT-COPIED')) 'Configuration contents copied'
        Assert-True ($report.status -ceq 'passed' -and ($report.steps.name -join '|') -ceq 'preflight|format') 'CLI success or steps missing'
    }
} finally {
    Assert-SafeTestRoot
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
Write-Host "Development diagnostics: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
