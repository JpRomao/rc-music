#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-DiagnosticPath([string] $RepositoryRoot, [string] $Path) {
    $base = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'out/product/validation-runs')).TrimEnd('\', '/')
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    if ($full -ine $base -and -not $full.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Diagnostic path must stay in the validation-runs directory'
    }
    $ancestor = $full
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Diagnostic path traverses a reparse point' }
            if ($ancestor -ieq $base -and -not $item.PSIsContainer) { throw 'Diagnostic parent must be a directory' }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

function Read-DiagnosticGitState([string] $RepositoryRoot) {
    $top = @(& git -C $RepositoryRoot rev-parse --show-toplevel 2>&1)
    if ($LASTEXITCODE -ne 0 -or $top.Count -ne 1 -or
        [IO.Path]::GetFullPath([string]$top[0]).TrimEnd('\', '/') -ine $RepositoryRoot) {
        throw 'Diagnostics require the actual Git repository root'
    }
    $commit = @(& git -C $RepositoryRoot rev-parse --verify HEAD 2>&1)
    if ($LASTEXITCODE -ne 0 -or $commit.Count -ne 1 -or [string]$commit[0] -cnotmatch '\A[0-9a-f]{40}([0-9a-f]{24})?\z') {
        throw 'Diagnostics require an identifiable Git commit'
    }
    $dirty = @(& git -C $RepositoryRoot status --porcelain=v1 --untracked-files=normal 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot observe the diagnostic Git state' }
    [pscustomobject]@{ Commit = [string]$commit[0]; Dirty = $dirty.Count -ne 0 }
}

function Write-DiagnosticReport($Context, [switch] $Create) {
    $path = Join-Path $Context.Directory 'run.json'
    Assert-DiagnosticPath $Context.RepositoryRoot $path
    $mode = if ($Create) { [IO.FileMode]::CreateNew } else { [IO.FileMode]::Open }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($Context.Report | ConvertTo-Json -Depth 8) + "`n")
    $stream = [IO.File]::Open($path, $mode, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.Write($bytes); $stream.SetLength($bytes.Length); $stream.Flush() } finally { $stream.Dispose() }
}

function New-RcMusicDevelopmentDiagnostics {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][ValidateSet('format', 'check', 'build', 'test', 'lint', 'validate')][string] $Task,
        [Parameter(Mandatory)][string] $BuildDirectory
    )
    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $parent = Join-Path $root 'out/product/validation-runs'
    Assert-DiagnosticPath $root $parent
    $gitState = Read-DiagnosticGitState $root
    $null = [IO.Directory]::CreateDirectory($parent)
    $directory = Join-Path $parent ('run-' + [guid]::NewGuid().ToString('N'))
    Assert-DiagnosticPath $root $directory
    $null = New-Item -ItemType Directory -Path $directory
    $context = [pscustomobject]@{
        RepositoryRoot = $root; Directory = $directory
        Report = [ordered]@{
            schemaVersion = 1; task = $Task; buildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
            toolchainFile = $null; toolchainSha256 = $null; selectedTools = $null
            commitStart = $gitState.Commit; dirtyStart = $gitState.Dirty
            commitEnd = $null; dirtyEnd = $null; startedUtc = [DateTimeOffset]::UtcNow.ToString('O')
            finishedUtc = $null; status = 'running'; steps = @()
        }
    }
    Write-DiagnosticReport $context -Create
    return $context
}

function Start-RcMusicDiagnosticStep {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)][string] $Name)
    if ($Context.Report.status -cne 'running' -or $Name -cnotmatch '\A[a-z][a-z0-9-]*\z') { throw 'Invalid diagnostic step' }
    $step = [ordered]@{
        name = $Name; startedUtc = [DateTimeOffset]::UtcNow.ToString('O'); finishedUtc = $null
        status = 'running'; exitCode = $null; log = ('{0:D2}-{1}.log' -f ($Context.Report.steps.Count + 1), $Name)
    }
    $path = Join-Path $Context.Directory $step.log
    Assert-DiagnosticPath $Context.RepositoryRoot $path
    $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $stream.Dispose()
    $Context.Report.steps += $step
    Write-DiagnosticReport $Context
    return $step
}

function Complete-RcMusicDiagnosticStep {
    param(
        [Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Step,
        [AllowNull()][Nullable[int]] $ExitCode = $null, [object[]] $Output = @(), [string] $Failure = ''
    )
    if ($Context.Report.status -cne 'running' -or $Step.status -cne 'running' -or
        -not @($Context.Report.steps | Where-Object { [object]::ReferenceEquals($_, $Step) }).Count) {
        throw 'Diagnostic step is not active in this run'
    }
    $path = Join-Path $Context.Directory $Step.log
    Assert-DiagnosticPath $Context.RepositoryRoot $path
    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $null = $stream.Seek(0, [IO.SeekOrigin]::End)
        foreach ($line in @($Output) + @($(if ($Failure) { "ERROR: $Failure" }))) {
            if ($null -ne $line) { $stream.Write([Text.Encoding]::UTF8.GetBytes([string]$line + "`n")) }
        }
        $stream.Flush()
    } finally { $stream.Dispose() }
    $Step.exitCode = $ExitCode
    $Step.status = if ($null -ne $ExitCode -and $ExitCode -eq 0 -and -not $Failure) { 'passed' } else { 'failed' }
    $Step.finishedUtc = [DateTimeOffset]::UtcNow.ToString('O')
    Write-DiagnosticReport $Context
}

function Complete-RcMusicDevelopmentDiagnostics {
    param([Parameter(Mandatory)] $Context, [switch] $Failed)
    if ($Context.Report.status -cne 'running') { throw 'Diagnostic run already completed' }
    $gitState = Read-DiagnosticGitState $Context.RepositoryRoot
    $Context.Report.commitEnd = $gitState.Commit
    $Context.Report.dirtyEnd = $gitState.Dirty
    $Context.Report.finishedUtc = [DateTimeOffset]::UtcNow.ToString('O')
    $Context.Report.status = if ($Failed -or @($Context.Report.steps | Where-Object status -CNE 'passed').Count) { 'failed' } else { 'passed' }
    Write-DiagnosticReport $Context
}

function Assert-RcMusicDiagnosticJUnit {
    param([Parameter(Mandatory)] $Context, [switch] $Before)
    $path = Join-Path $Context.Directory 'ctest.xml'
    Assert-DiagnosticPath $Context.RepositoryRoot $path
    if ($Before) {
        if (Test-Path -LiteralPath $path) { throw 'JUnit destination already exists in this diagnostic run' }
    } elseif (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path -Force).Length -eq 0) {
        throw 'CTest succeeded without delivering its nonempty diagnostic JUnit file'
    }
}

Export-ModuleMember -Function New-RcMusicDevelopmentDiagnostics, Start-RcMusicDiagnosticStep,
    Complete-RcMusicDiagnosticStep, Complete-RcMusicDevelopmentDiagnostics, Assert-RcMusicDiagnosticJUnit
